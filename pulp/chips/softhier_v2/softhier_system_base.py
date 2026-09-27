#
# Copyright (C) 2026 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Authors: Chi Zhang <chizhang@ethz.ch>, 
#          Siim Rausi <srausi@student.ethz.ch>,
#          Rafael Medina Morillas <medinar@ethz.ch>


"""SoftHier system, io_v2 generation.

Same architecture as the v1 SoftHier system (pulp.chips.softhier): a grid
of clusters connected by a FlooNoC, built with io_v2 models. The NoC is the
FlooNoC v2 graph NoC (pulp.floonoc_v2), which routes with per-router tables
(the RTL IdTable routing) and wormhole arbitration. Its topology is
generated at build time from the arch parameters, with the same FlooGen
generator as v1 (topologies/gen_floogen_topology.py).
"""

import contextlib
import io
import os
import tempfile
import interco.router_v2 as router_v2
from interco.router_v2 import RouterConfig, RouterMapping, KIND_BANDWIDTH, KIND_BEAT
from vp.clock_domain import Clock_domain
import gvsoc.systree
from gvrun.parameter import TargetParameter
from pulp.chips.softhier_v2.common.cluster_unit import ClusterUnit, ClusterArch, NARROW_WIDTH
from pulp.chips.softhier_v2.common.softhier_ctrl import SoftHierCtrl, SoftHierCtrlConfig
from pulp.chips.softhier_v2.common.error_detector import ErrorDetector
from pulp.chips.softhier_v2.softhier_arch_base import TOPOLOGIES, get_arch_overrides, SoftHierAttributes
from pulp.chips.softhier_v2.topologies.gen_floogen_topology import GENERATORS
from pulp.floonoc_v2.floonoc_v2 import FlooNocV2Graph


def _ni_node_name(arch, cluster_id):
    """
    The generated floogen NI node name for a cluster, matching whichever
    naming convention gen_floogen_topology.py used for this shape: 3D
    mesh/torus families declare cluster_{x}_{y}_{z}_ni, 2D mesh/torus
    declare cluster_{x}_{y}_ni, and every other family (ring,
    hierarchical_ring, hexamesh, folded_hexatorus) declares the flat
    cluster_{cluster_id}_ni -- dispatched here on which coordinate
    fields the arch actually has.
    """
    if getattr(arch, 'num_cluster_z', None) is not None:
        x = cluster_id % arch.num_cluster_x
        y = (cluster_id // arch.num_cluster_x) % arch.num_cluster_y
        z = cluster_id // (arch.num_cluster_x * arch.num_cluster_y)
        return f"cluster_{x}_{y}_{z}_ni"
    if getattr(arch, 'num_cluster_x', None) is not None:
        x = cluster_id % arch.num_cluster_x
        y = cluster_id // arch.num_cluster_x
        return f"cluster_{x}_{y}_ni"
    return f"cluster_{cluster_id}_ni"


def _assert_topology_dimensions(arch):
    if getattr(arch, 'num_cluster_z', None) is not None:
        assert arch.num_cluster_x * arch.num_cluster_y * arch.num_cluster_z == arch.num_cluster, \
            "Topology dimesion not match total number of clusters"
    elif getattr(arch, 'num_cluster_x', None) is not None:
        assert arch.num_cluster_x * arch.num_cluster_y == arch.num_cluster, \
            "Topology dimesion not match total number of clusters"
    elif getattr(arch, 'num_rings', None) is not None:
        assert 1 + 3 * arch.num_rings * (arch.num_rings + 1) == arch.num_cluster, \
            "Topology dimensions are mismatched"


class SoftHierSystem(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, topology):
        super().__init__(parent, name)

        #################
        # Configuration #
        #################

        arch_cls = TOPOLOGIES[topology]
        arch = arch_cls(**get_arch_overrides(self, arch_cls))

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        # Get Binary.
        # With the legacy gvsoc launcher, configure() is never called and the binary comes
        # from the command line, so it must be resolved now. With gvrun, it comes from a
        # parameter and is handled in configure() since it can also be set from the build
        # process.
        binary = None
        if os.environ.get('USE_GVRUN') is None and parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        _assert_topology_dimensions(arch)

        # Final arch, with the overrides, published by the platform as attributes
        self.arch = arch

        ##############
        # Components #
        ##############

        # Clusters
        cluster_list = []
        for cluster_id in range(arch.num_cluster):
            cluster_arch = ClusterArch(num_core=arch.num_core_per_cluster,
                                        cluster_id=cluster_id,
                                        spatz_num_lane=arch.spatz_num_lane,
                                        spatz_lane_width=arch.spatz_lane_width,
                                        tcdm_bank_nb=arch.cluster_tcdm_bank_nb,
                                        tcdm_bank_width=arch.cluster_tcdm_bank_width,
                                        inst_base=arch.instruction_mem_base,
                                        inst_size=arch.instruction_mem_size,
                                        tcdm_base=arch.cluster_tcdm_base,
                                        tcdm_size=arch.cluster_tcdm_size,
                                        stack_base=arch.cluster_stack_base,
                                        stack_size=arch.cluster_stack_size,
                                        zomem_base=arch.cluster_zomem_base,
                                        zomem_size=arch.cluster_zomem_size,
                                        reg_base=arch.cluster_reg_base,
                                        reg_size=arch.cluster_reg_size,
                                        idma_outstand_txn=arch.idma_outstand_txn,
                                        idma_outstand_burst=arch.idma_outstand_burst,
                                        wide_width=arch.noc_link_width,
                                        noc_outstanding=arch.noc_outstanding)
            cluster_list.append(ClusterUnit(self, f'cluster_{cluster_id}', cluster_arch, binary))

        # Keep the clusters so configure() can push the binary to their loaders when
        # it is provided through a parameter (gvrun flow).
        self.clusters = cluster_list
        self.register_binary_handler(self.handle_binary)

        # Virtual router, just for debugging and non-performance-critical jobs. One
        # input per cluster narrow and wide arbiter.
        virtual_interco = router_v2.Router(self, 'virtual_interco', config=RouterConfig(
            kind=KIND_BANDWIDTH, bandwidth=8))

        # Debug Memory
        error_detector = ErrorDetector(self, 'error_detector')

        # Control register
        softhier_ctrl = SoftHierCtrl(self, 'softhier_ctrl', config=SoftHierCtrlConfig(
            num_cluster=arch.num_cluster, num_core_per_cluster=arch.num_core_per_cluster))

        # NoC, built from the FlooGen topology generated for this arch. Routers
        # buffer 16 flits per input, the NIs keep noc_outstanding bursts in
        # flight.
        noc = FlooNocV2Graph(self, 'noc', narrow_width=NARROW_WIDTH,
            wide_width=arch.noc_link_width, ni_outstanding_reqs=arch.noc_outstanding,
            router_input_queue_size=16, allow_deadlock=bool(arch.noc_allow_deadlock))
        with tempfile.TemporaryDirectory() as gen_dir:
            # The generator reports the files it writes, which are temporary
            with contextlib.redirect_stdout(io.StringIO()):
                GENERATORS[topology](arch, gen_dir, topology)
            noc.load_floogen(os.path.join(gen_dir, f'{topology}.floogen.yml'),
                routing_path=os.path.join(gen_dir, f'{topology}.routing.yml'),
                link_latencies_path=os.path.join(gen_dir, f'{topology}.link_latencies.yml'))

        ############
        # Bindings #
        ############

        # Debug memory
        virtual_interco.o_MAP_DEFAULT(error_detector.i_INPUT(), name='error_detector')

        # Control register
        virtual_interco.o_MAP(softhier_ctrl.i_INPUT(),
            RouterMapping(base=arch.soc_register_base, size=arch.soc_register_size),
            name='softhier_ctrl')

        # Remote TCDMs, reached through the NoC
        remote_base = arch.cluster_tcdm_remote
        remote_size = arch.num_cluster * arch.cluster_tcdm_size

        for cluster_id in range(arch.num_cluster):
            ni_node_id = noc.id_map[_ni_node_name(arch, cluster_id)]

            # As many transactions in flight as the network interface accepts
            narrow_arbiter = router_v2.Router(self, f'narrow_arbiter_{cluster_id}',
                config=RouterConfig(kind=KIND_BEAT, width=NARROW_WIDTH,
                    max_pending_bursts_per_input=arch.noc_outstanding))
            narrow_arbiter.o_MAP(noc.i_NARROW_INPUT(ni_node_id),
                RouterMapping(base=remote_base, size=remote_size, remove_base=False),
                name='noc')
            narrow_arbiter.o_MAP_DEFAULT(virtual_interco.i_INPUT(2 * cluster_id),
                name='virtual_interco')

            wide_arbiter = router_v2.Router(self, f'wide_arbiter_{cluster_id}',
                config=RouterConfig(kind=KIND_BEAT, width=arch.noc_link_width,
                    max_pending_bursts_per_input=arch.noc_outstanding))
            wide_arbiter.o_MAP(noc.i_WIDE_INPUT(ni_node_id),
                RouterMapping(base=remote_base, size=remote_size, remove_base=False),
                name='noc')
            wide_arbiter.o_MAP_DEFAULT(virtual_interco.i_INPUT(2 * cluster_id + 1),
                name='virtual_interco')

            cluster_list[cluster_id].o_NARROW_SOC(narrow_arbiter.i_INPUT())
            cluster_list[cluster_id].o_WIDE_SOC(wide_arbiter.i_INPUT())

            # The cluster TCDM range, seen from the cluster as its local range
            tcdm_base = remote_base + cluster_id * arch.cluster_tcdm_size
            noc.o_MAP(tcdm_base, arch.cluster_tcdm_size, ni_node_id,
                name=f'cluster_{cluster_id}', remove_offset=tcdm_base - arch.cluster_tcdm_base)
            noc.o_NARROW_BIND(cluster_list[cluster_id].i_NARROW_INPUT(), ni_node_id)
            noc.o_WIDE_BIND(cluster_list[cluster_id].i_WIDE_INPUT(), ni_node_id)

    def configure(self):
        # With gvrun the binary is provided through a parameter (set either from the
        # command line or from the build process), so push it to the cluster loaders here.
        binary = self.get_parameter('binary')
        if binary is not None:
            for cluster in self.clusters:
                cluster.set_binary(binary)

    def handle_binary(self, binary):
        # Called when an executable is attached to a hierarchy containing this component.
        self.set_parameter('binary', binary)


class SoftHierPlatform(gvsoc.systree.Component):
    # Each topology's target shim subclasses this and sets `topology` to
    # its TOPOLOGIES key (see softhier_arch_base.TOPOLOGIES).
    topology = None

    def __init__(self, parent, name, parser, options):
        super(SoftHierPlatform, self).__init__(parent, name, options=options)

        arch = TOPOLOGIES[self.topology]()
        clock = Clock_domain(self, 'clock', frequency=(1000000000 if not hasattr(arch, 'frequence') else arch.frequence))

        softhier_system = SoftHierSystem(self, 'system', parser, self.topology)

        self.bind(clock, 'out', softhier_system, 'clock')

        # Let the build tools query the architecture (memory map, number of
        # clusters and cores, ...) and know how to compile for this board
        # (pulpos.softhier module)
        self.set_attributes(SoftHierAttributes(self, 'softhier', softhier_system.arch))
        self.set_target_name('softhier')
