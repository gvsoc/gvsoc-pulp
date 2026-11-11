#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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

# Author: Chi Zhang <chizhang@ethz.ch>

import math
import os
import interco.router as router
import memory.memory
import memory.dramsys
import utils.loader.loader
from vp.clock_domain import Clock_domain
import gvsoc.systree
from gvrun.parameter import TargetParameter
from pulp.chips.mc_pool.common.cluster_unit import ClusterUnit, ClusterArch
from pulp.chips.mc_pool.common.mc_pool_ctrl import McPoolCtrl
from pulp.chips.mc_pool.common.mc_pool_mesh_noc import McPoolMeshNoc
from pulp.chips.mc_pool.common.hbm_ctrl import hbm_ctrl
from pulp.chips.mc_pool.mc_pool_arch_base import CONFIGS, get_arch_overrides

def is_power_of_two(name, value):
    if value == 0:
        return True
    if value < 0 or (value & (value - 1)) != 0:
        raise AssertionError(f"The value of '{name}' must be a power of two, but got {value}.")
    return True


class McPoolSystem(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, config):
        super().__init__(parent, name)

        #################
        # Configuration #
        #################

        arch_cls        = CONFIGS[config]
        arch            = arch_cls(**get_arch_overrides(self, arch_cls))
        num_clusters    = arch.num_cluster_x * arch.num_cluster_y
        noc_outstanding = (arch.num_cluster_x + arch.num_cluster_y) + arch.noc_outstanding
        num_hbm_ctrl_x  = arch.num_cluster_x // arch.num_node_per_ctrl
        num_hbm_ctrl_y  = arch.num_cluster_y // arch.num_node_per_ctrl

        # Get Binary. With the legacy gvsoc launcher, configure() is never called and the binary
        # comes from the command line, so it must be resolved now. With gvrun, it comes from the
        # binary parameter and is handled in configure().
        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )
        binary = None
        preload_binary = None
        has_preload_binary = 0
        if os.environ.get('USE_GVRUN') is None and parser is not None:
            parser.add_argument("--preload", type=str, help="Path to the HBM preload binary file")
            [args, otherArgs] = parser.parse_known_args()
            # No --binary when the platform is generated at build time (gvrun
            # without USE_GVRUN): the binary then comes from the parameter
            binary = getattr(args, 'binary', None)
            preload_binary = args.preload
            if preload_binary is not None:
                has_preload_binary = 1

        #############
        # Assertion #
        #############
        is_power_of_two("arch.num_node_per_ctrl",arch.num_node_per_ctrl)
        is_power_of_two("arch.hbm_chan_placement[0]",arch.hbm_chan_placement[0])
        is_power_of_two("arch.hbm_chan_placement[1]",arch.hbm_chan_placement[1])
        is_power_of_two("arch.hbm_chan_placement[2]",arch.hbm_chan_placement[2])
        is_power_of_two("arch.hbm_chan_placement[3]",arch.hbm_chan_placement[3])
        assert ((arch.num_cluster_x % arch.num_node_per_ctrl) == 0), f"arch.num_node_per_ctrl ({arch.num_node_per_ctrl}) is not suitable for arch.num_cluster_x ({arch.num_cluster_x})"
        assert (arch.num_cluster_x >= arch.num_node_per_ctrl), f"arch.num_node_per_ctrl ({arch.num_node_per_ctrl}) is smaller than arch.num_cluster_x ({arch.num_cluster_x})"
        assert ((arch.num_cluster_y % arch.num_node_per_ctrl) == 0), f"arch.num_node_per_ctrl ({arch.num_node_per_ctrl}) is not suitable for arch.num_cluster_y ({arch.num_cluster_y})"
        assert (arch.num_cluster_y >= arch.num_node_per_ctrl), f"arch.num_node_per_ctrl ({arch.num_node_per_ctrl}) is smaller than arch.num_cluster_y ({arch.num_cluster_y})"
        assert ((arch.hbm_chan_placement[0] >= num_hbm_ctrl_y) or (arch.hbm_chan_placement[0] == 0)), f"arch.hbm_chan_placement[0] ({arch.hbm_chan_placement[0]}) is smaller than hbm controller on y direction ({num_hbm_ctrl_y})"
        assert ((arch.hbm_chan_placement[1] >= num_hbm_ctrl_x) or (arch.hbm_chan_placement[1] == 0)), f"arch.hbm_chan_placement[1] ({arch.hbm_chan_placement[1]}) is smaller than hbm controller on x direction ({num_hbm_ctrl_x})"
        assert ((arch.hbm_chan_placement[2] >= num_hbm_ctrl_y) or (arch.hbm_chan_placement[2] == 0)), f"arch.hbm_chan_placement[2] ({arch.hbm_chan_placement[2]}) is smaller than hbm controller on y direction ({num_hbm_ctrl_y})"
        assert ((arch.hbm_chan_placement[3] >= num_hbm_ctrl_x) or (arch.hbm_chan_placement[3] == 0)), f"arch.hbm_chan_placement[3] ({arch.hbm_chan_placement[3]}) is smaller than hbm controller on x direction ({num_hbm_ctrl_x})"
        # The edge node aliasing was done by the private FlooNoC, the shared one has no support for it
        assert (arch.hbm_node_aliase == 1), f"arch.hbm_node_aliase ({arch.hbm_node_aliase}) must be 1, edge node aliasing is not supported"

        ctrl_chan_west  = arch.hbm_chan_placement[0] // num_hbm_ctrl_y
        ctrl_chan_north = arch.hbm_chan_placement[1] // num_hbm_ctrl_x
        ctrl_chan_east  = arch.hbm_chan_placement[2] // num_hbm_ctrl_y
        ctrl_chan_south = arch.hbm_chan_placement[3] // num_hbm_ctrl_x

        ##############
        # Components #
        ##############

        #Clusters
        cluster_list=[]
        for cluster_id in range(num_clusters):
            cluster_arch = ClusterArch( nb_core_per_cluster     =   arch.num_core_per_cluster,
                                        base                    =   arch.cluster_tcdm_base,
                                        tcdm_size               =   arch.cluster_tcdm_size,
                                        cluster_id              =   cluster_id,
                                        stack_base              =   arch.cluster_stack_base,
                                        stack_size              =   arch.cluster_stack_size,
                                        reg_base                =   arch.cluster_reg_base,
                                        reg_size                =   arch.cluster_reg_size,
                                        num_cluster_x           =   arch.num_cluster_x,
                                        num_cluster_y           =   arch.num_cluster_y,
                                        insn_base               =   arch.instruction_mem_base,
                                        insn_size               =   arch.instruction_mem_size,
                                        terapool                =   arch.terapool,
                                        tensorpool              =   arch.tensorpool,
                                        nb_redmule_tiles        =   arch.nb_redmule_tiles,
                                        redmule_height          =   arch.redmule_height,
                                        redmule_width           =   arch.redmule_width,
                                        redmule_regs            =   arch.redmule_regs,
                                        redmule_queue_depth     =   arch.redmule_queue_depth,
                                        redmule_stream_loads    =   arch.redmule_stream_loads,
                                        redmule_tcdm_bank_number=   arch.redmule_tcdm_bank_number,
                                        nb_cores_per_tile       =   arch.nb_cores_per_tile,
                                        nb_sub_groups_per_group =   arch.nb_sub_groups_per_group,
                                        nb_groups               =   arch.nb_groups,
                                        bank_factor             =   arch.bank_factor,
                                        bank_size               =   arch.bank_size,
                                        axi_data_width          =   arch.axi_data_width,
                                        dma_burst_queue_size    =   arch.dma_burst_queue_size,
                                        nb_axi_masters_per_group=   arch.nb_axi_masters_per_group,
                                        nb_l2_banks             =   arch.nb_l2_banks,
                                        sync_base               =   arch.sync_base,
                                        sync_itlv               =   arch.sync_interleave,
                                        sync_special_mem        =   arch.sync_special_mem,
                                        soc_register_base       =   arch.soc_register_base,
                                        remote_group_latency    =   arch.remote_group_latency)

            cluster_list.append(ClusterUnit(self,f'cluster_{cluster_id}', cluster_arch, binary, parser))
            pass
        self.cluster_list = cluster_list

        #Virtual router, just for debugging and non-performance-critical jobs (eg, Printf, EoC, Check HBM stored value)
        virtual_interco = router.Router(self, 'virtual_interco', bandwidth=8)

        #Control register
        ctrl = McPoolCtrl(self, 'mc_pool_ctrl', num_cluster_x=arch.num_cluster_x, num_cluster_y=arch.num_cluster_y, has_preload_binary=has_preload_binary)


        #HBM channels. DRAMSys needs SystemC, which makes the whole simulation much slower, so by
        #default the channels are plain memories, without the DRAM timing
        def new_hbm_chan(chan_name):
            if arch.hbm_dramsys:
                return memory.dramsys.Dramsys(self, chan_name)
            return memory.memory.Memory(self, chan_name, size=arch.hbm_chan_size)

        hbm_chan_list_west = []
        for hbm_ch in range(arch.hbm_chan_placement[0]):
            hbm_chan_list_west.append(new_hbm_chan(f'west_hbm_chan_{hbm_ch}'))
            pass

        hbm_chan_list_north = []
        for hbm_ch in range(arch.hbm_chan_placement[1]):
            hbm_chan_list_north.append(new_hbm_chan(f'north_hbm_chan_{hbm_ch}'))
            pass

        hbm_chan_list_east = []
        for hbm_ch in range(arch.hbm_chan_placement[2]):
            hbm_chan_list_east.append(new_hbm_chan(f'east_hbm_chan_{hbm_ch}'))
            pass

        hbm_chan_list_south = []
        for hbm_ch in range(arch.hbm_chan_placement[3]):
            hbm_chan_list_south.append(new_hbm_chan(f'south_hbm_chan_{hbm_ch}'))
            pass

        #HBM controllers
        hbm_ctrl_list_west = []
        for hbm_ct in range(num_hbm_ctrl_y):
            nb_slaves=ctrl_chan_west
            hbm_ctrl_list_west.append(hbm_ctrl(self, f'west_hbm_ctrl_{hbm_ct}', nb_slaves=nb_slaves, nb_masters=arch.num_node_per_ctrl, interleaving_bits=int(math.log2(arch.noc_link_width/8)), node_addr_offset=arch.hbm_node_addr_space, hbm_node_aliase=arch.hbm_node_aliase, xor_scrambling=arch.hbm_ctrl_xor_scrambling, red_scrambling=arch.hbm_ctrl_red_scrambling))
            pass

        hbm_ctrl_list_north = []
        for hbm_ct in range(num_hbm_ctrl_x):
            nb_slaves=ctrl_chan_north
            hbm_ctrl_list_north.append(hbm_ctrl(self, f'north_hbm_ctrl_{hbm_ct}', nb_slaves=nb_slaves, nb_masters=arch.num_node_per_ctrl, interleaving_bits=int(math.log2(arch.noc_link_width/8)), node_addr_offset=arch.hbm_node_addr_space, hbm_node_aliase=arch.hbm_node_aliase, xor_scrambling=arch.hbm_ctrl_xor_scrambling, red_scrambling=arch.hbm_ctrl_red_scrambling))
            pass

        hbm_ctrl_list_east = []
        for hbm_ct in range(num_hbm_ctrl_y):
            nb_slaves=ctrl_chan_east
            hbm_ctrl_list_east.append(hbm_ctrl(self, f'east_hbm_ctrl_{hbm_ct}', nb_slaves=nb_slaves, nb_masters=arch.num_node_per_ctrl, interleaving_bits=int(math.log2(arch.noc_link_width/8)), node_addr_offset=arch.hbm_node_addr_space, hbm_node_aliase=arch.hbm_node_aliase, xor_scrambling=arch.hbm_ctrl_xor_scrambling, red_scrambling=arch.hbm_ctrl_red_scrambling))
            pass

        hbm_ctrl_list_south = []
        for hbm_ct in range(num_hbm_ctrl_x):
            nb_slaves=ctrl_chan_south
            hbm_ctrl_list_south.append(hbm_ctrl(self, f'south_hbm_ctrl_{hbm_ct}', nb_slaves=nb_slaves, nb_masters=arch.num_node_per_ctrl, interleaving_bits=int(math.log2(arch.noc_link_width/8)), node_addr_offset=arch.hbm_node_addr_space, hbm_node_aliase=arch.hbm_node_aliase, xor_scrambling=arch.hbm_ctrl_xor_scrambling, red_scrambling=arch.hbm_ctrl_red_scrambling))
            pass

        #NoC, the wide network carries the data and the narrow one the synchronization
        noc = McPoolMeshNoc(self, 'noc', narrow_width=4, wide_width=arch.noc_link_width//8,
                nb_x_clusters=arch.num_cluster_x, nb_y_clusters=arch.num_cluster_y,
                ni_outstanding_reqs=noc_outstanding, router_input_queue_size=noc_outstanding * num_clusters)


        #Debug Memory
        debug_mem = memory.memory.Memory(self,'debug_mem', size=1)

        #HBM Preloader
        hbm_preloader = utils.loader.loader.ElfLoader(self, 'hbm_preloader', binary=preload_binary)

        ############
        # Bindings #
        ############

        #Debug memory
        virtual_interco.o_MAP(debug_mem.i_INPUT())
        virtual_interco.o_MAP(noc.i_CLUSTER_WIDE_INPUT(0, 0), base=arch.hbm_start_base, size=arch.hbm_node_addr_space * 2 * (arch.num_cluster_x + arch.num_cluster_y), rm_base=False)

        #Control register
        virtual_interco.o_MAP(ctrl.i_INPUT(), base=arch.soc_register_base, size=arch.soc_register_size, rm_base=True)

        #HBM Preloader
        hbm_preloader.o_OUT(noc.i_CLUSTER_WIDE_INPUT(0, 0))
        hbm_preloader.o_START(ctrl.i_HBM_PRELOAD_DONE())

        #Clusters
        for cluster_id in range(num_clusters):
            cluster_list[cluster_id].o_NARROW_SOC(virtual_interco.i_INPUT())
            ctrl.o_HBM_PRELOAD_DONE_TO_CLUSTER(cluster_list[cluster_id].i_HBM_PRELOAD_DONE(),cluster_id)
            pass

        #Data NoC + Sync NoC
        for node_id in range(num_clusters):
            x_id = int(node_id%arch.num_cluster_x)
            y_id = int(node_id/arch.num_cluster_x)
            cluster_list[node_id].o_WIDE_SOC(noc.i_CLUSTER_WIDE_INPUT(x_id, y_id))
            cluster_list[node_id].o_SYNC_OUTPUT(noc.i_CLUSTER_NARROW_INPUT(x_id, y_id))
            noc.o_WIDE_MAP(cluster_list[node_id].i_WIDE_INPUT(), base=arch.cluster_tcdm_remote  + node_id*arch.cluster_tcdm_size,   size=arch.cluster_tcdm_size,    node_id=noc.cluster_node(x_id, y_id))
            noc.o_NARROW_MAP(cluster_list[node_id].i_SYNC_INPUT(), base=arch.sync_base            + node_id*(arch.sync_interleave + arch.sync_special_mem),     size=(arch.sync_interleave + arch.sync_special_mem),      node_id=noc.cluster_node(x_id, y_id))
            pass

        #HBM controllers and channels connections
        #Mapping:
        #         1->
        #      ________
        #   ^ |        | ^
        #   | |        | |
        #   0 |        | 2
        #     |________|
        #
        #         3->
        hbm_edge_start_base = arch.hbm_start_base

        ## west
        for node_id in range(arch.num_cluster_y):
            ctrl_id = node_id // arch.num_node_per_ctrl
            itf_router = router.Router(self, f'west_{node_id}')
            itf_router.add_mapping('output')
            base_addr = hbm_edge_start_base + node_id * arch.hbm_node_addr_space
            node_size = arch.hbm_node_addr_space
            noc.o_WIDE_MAP(itf_router.i_INPUT(), base=base_addr, size=node_size, node_id=noc.edge_node('west', node_id))
            self.bind(itf_router, 'output', hbm_ctrl_list_west[ctrl_id], f'in_{node_id % arch.num_node_per_ctrl}')
            pass
        for chan_id in range(arch.hbm_chan_placement[0]):
            ctrl_id = chan_id // ctrl_chan_west
            self.bind(hbm_ctrl_list_west[ctrl_id], f'out_{chan_id % ctrl_chan_west}', hbm_chan_list_west[chan_id], 'input')
            pass
        hbm_edge_start_base += arch.num_cluster_y*arch.hbm_node_addr_space

        ## north
        for node_id in range(arch.num_cluster_x):
            ctrl_id = node_id // arch.num_node_per_ctrl
            itf_router = router.Router(self, f'north_{node_id}')
            itf_router.add_mapping('output')
            base_addr = hbm_edge_start_base + node_id * arch.hbm_node_addr_space
            node_size = arch.hbm_node_addr_space
            noc.o_WIDE_MAP(itf_router.i_INPUT(), base=base_addr, size=node_size, node_id=noc.edge_node('north', node_id))
            self.bind(itf_router, 'output', hbm_ctrl_list_north[ctrl_id], f'in_{node_id % arch.num_node_per_ctrl}')
            pass
        for chan_id in range(arch.hbm_chan_placement[1]):
            ctrl_id = chan_id // ctrl_chan_north
            self.bind(hbm_ctrl_list_north[ctrl_id], f'out_{chan_id % ctrl_chan_north}', hbm_chan_list_north[chan_id], 'input')
            pass
        hbm_edge_start_base += arch.num_cluster_x*arch.hbm_node_addr_space

        ## east
        for node_id in range(arch.num_cluster_y):
            ctrl_id = node_id // arch.num_node_per_ctrl
            itf_router = router.Router(self, f'east_{node_id}')
            itf_router.add_mapping('output')
            base_addr = hbm_edge_start_base + node_id * arch.hbm_node_addr_space
            node_size = arch.hbm_node_addr_space
            noc.o_WIDE_MAP(itf_router.i_INPUT(), base=base_addr, size=node_size, node_id=noc.edge_node('east', node_id))
            self.bind(itf_router, 'output', hbm_ctrl_list_east[ctrl_id], f'in_{node_id % arch.num_node_per_ctrl}')
            pass
        for chan_id in range(arch.hbm_chan_placement[2]):
            ctrl_id = chan_id // ctrl_chan_east
            self.bind(hbm_ctrl_list_east[ctrl_id], f'out_{chan_id % ctrl_chan_east}', hbm_chan_list_east[chan_id], 'input')
            pass
        hbm_edge_start_base += arch.num_cluster_y*arch.hbm_node_addr_space

        ## south
        for node_id in range(arch.num_cluster_x):
            ctrl_id = node_id // arch.num_node_per_ctrl
            itf_router = router.Router(self, f'south_{node_id}')
            itf_router.add_mapping('output')
            base_addr = hbm_edge_start_base + node_id * arch.hbm_node_addr_space
            node_size = arch.hbm_node_addr_space
            noc.o_WIDE_MAP(itf_router.i_INPUT(), base=base_addr, size=node_size, node_id=noc.edge_node('south', node_id))
            self.bind(itf_router, 'output', hbm_ctrl_list_south[ctrl_id], f'in_{node_id % arch.num_node_per_ctrl}')
            pass
        for chan_id in range(arch.hbm_chan_placement[3]):
            ctrl_id = chan_id // ctrl_chan_south
            self.bind(hbm_ctrl_list_south[ctrl_id], f'out_{chan_id % ctrl_chan_south}', hbm_chan_list_south[chan_id], 'input')
            pass

        self.register_binary_handler(self.handle_binary)

    def configure(self) -> None:
        # The binary parameter can be set from the command line or from the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            for cluster in self.cluster_list:
                cluster.loader.set_binary(binary)

    def handle_binary(self, binary: str):
        # Called when an executable is attached to a hierarchy of components containing this one
        self.set_parameter('binary', binary)



class McPoolPlatform(gvsoc.systree.Component):
    # Each configuration's target shim subclasses this and sets `config` to
    # its CONFIGS key (see mc_pool_arch_base.CONFIGS).
    config = None

    def __init__(self, parent, name, parser, options):
        super(McPoolPlatform, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=1000000000)

        mc_pool_system = McPoolSystem(self, 'system', parser, self.config)

        self.bind(clock, 'out', mc_pool_system, 'clock')
