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

import os
import gvsoc.systree
import gvsoc.runner

import vp.clock_domain
from pulp.floonoc_v2.floonoc_v2 import FloonocNetworkInterfaceV2, FloonocRouterV2, \
    FlooNocV22dMeshNarrowWide, FloonocNetworkInterfaceV2Config, FloonocRouterV2Config, \
    FlooNocV2Graph, DIR_LEFT, DIR_RIGHT, xy_node_id
from interco.traffic.generator_v2 import GeneratorV2
from interco.traffic.receiver_v2 import ReceiverV2
from gvrun.parameter import TargetParameter

# 64-bit narrow link, matching the RTL floo_axi_chimney/axi_mesh AXI config.
NARROW_WIDTH = 8
WIDE_WIDTH = 64
NODE_SIZE = 0x10000
MESH_DIM = 4
# Graph topologies: number of nodes (NIs) and pipeline stages of the
# router-to-router links of the staged ring.
GRAPH_NODES = 6
RING_STAGES = 2


class FloonocCalibTest(gvsoc.systree.Component):
    """Driver for the FlooNoC calibration testbench."""

    def __init__(self, parent, name, nb_nodes, node_size, node1_base, topology):
        super().__init__(parent, name)

        self.add_property('nb_nodes', nb_nodes)
        self.add_property('node_size', node_size)
        self.add_property('node1_base', node1_base)
        self.add_property('topology', topology)

        self.add_sources(['test.cpp'])

    def o_GENERATOR_CONTROL(self, node: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'generator_control_{node}', itf,
            signature='wire<TrafficGeneratorConfig>')

    def o_RECEIVER_CONTROL(self, node: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'receiver_control_{node}', itf,
            signature='wire<TrafficReceiverConfig>')


class Testbench(gvsoc.systree.Component):
    """Calibration mirrors of the FlooNoC RTL testbenches.

    Each node is a traffic generator on an NI input (the RTL iDMA test node
    master) plus a receiver on the NI output (the RTL FastSlave sim memory),
    narrow port only. Topologies:

    - 'direct': two NIs cross-bound link-to-link (RTL tb_floo_dma_chimney).
    - 'router': one router per network between the two NIs at (1, 0)
      (RTL tb_floo_dma_router). Node positions encode the address range like
      the RTL XY address mapping (base = x * NODE_SIZE).
    - 'mesh': the 4x4 mesh with one router+NI per position (RTL
      tb_floo_axi_mesh, without the border HBM). Node (x, y) owns
      [(x*4+y) * NODE_SIZE, ...+NODE_SIZE) like the RTL job generator.
    - 'floogen_xy' / 'floogen_id': the same RTL mesh, loaded from the FlooGen
      description the RTL is generated from (floogen/axi_mesh_*.yml, with
      its border HBM NIs left idle) into the ID-table routed graph NoC, with
      XY-equivalent tables or FlooGen's own ID tables.
    - 'graph_ring', 'graph_ring_stages', 'graph_tree': topologies the mesh
      cannot express, on the graph NoC (no RTL golden, model regression
      values): a ring of GRAPH_NODES routers with one NI each and up/down
      routing (the shortest paths of a ring can deadlock), the same ring
      with RING_STAGES pipeline stages on every router-to-router link, and
      a two-level tree (a root router over three leaf routers holding two
      NIs each) with shortest-path routing. Node n owns
      [n * NODE_SIZE, ...+NODE_SIZE).
    """

    def __init__(self, parent, name, topology='direct'):
        super().__init__(parent, name)

        if topology == 'mesh':
            self.__init_mesh()
        elif topology.startswith('floogen_'):
            self.__init_floogen(topology.split('_', 1)[1])
        elif topology.startswith('graph_'):
            self.__init_graph(topology)
        else:
            self.__init_2nodes(topology)

    def __add_node(self, test, node, gen_itf, rcv_itf):
        generator = GeneratorV2(self, f'generator_{node}', max_burst_size=4096,
            width=NARROW_WIDTH)
        generator.o_OUTPUT(gen_itf)
        receiver = ReceiverV2(self, f'receiver_{node}', mem_size=NODE_SIZE)
        rcv_itf(receiver.i_INPUT())
        test.o_GENERATOR_CONTROL(node, generator.i_CONTROL())
        test.o_RECEIVER_CONTROL(node, receiver.i_CONTROL())

    def __init_mesh(self):
        # ni_outstanding_reqs = 32 matches the RTL chimney MaxTxns — the
        # RTL-accurate value. (An earlier NI admission bug serialized burst
        # admission on the previous burst's flit draining into a back-pressured
        # router, which under hotspot congestion needed a bogus 64 to hide it;
        # that bug is now fixed in the NI. The residual 4-way-hotspot
        # arbitration-fairness gap is handled with a per-flow tolerance in the
        # test, not by inflating this value — see the hotspot4 comment.)
        noc = FlooNocV22dMeshNarrowWide(self, 'noc', narrow_width=NARROW_WIDTH,
            wide_width=WIDE_WIDTH, dim_x=MESH_DIM, dim_y=MESH_DIM,
            ni_outstanding_reqs=32, router_input_queue_size=2)

        test = FloonocCalibTest(self, 'test', MESH_DIM * MESH_DIM, NODE_SIZE,
            0, 'mesh')

        for x in range(MESH_DIM):
            for y in range(MESH_DIM):
                noc.add_router(x, y)
                noc.add_network_interface(x, y)

        for x in range(MESH_DIM):
            for y in range(MESH_DIM):
                node = x * MESH_DIM + y
                noc.o_MAP(node * NODE_SIZE, NODE_SIZE, x, y, rm_base=True)
                self.__add_node(test, node, noc.i_NARROW_INPUT(x, y),
                    lambda itf, x=x, y=y: noc.o_NARROW_BIND(itf, x, y))

    def __init_floogen(self, route_algo):
        noc = FlooNocV2Graph(self, 'noc', narrow_width=NARROW_WIDTH, wide_width=WIDE_WIDTH,
            ni_outstanding_reqs=32, router_input_queue_size=2)
        id_map = noc.load_floogen(os.path.join(os.path.dirname(__file__), 'floogen',
            f'axi_mesh_{route_algo}.yml'))

        test = FloonocCalibTest(self, 'test', MESH_DIM * MESH_DIM, NODE_SIZE,
            0, f'floogen_{route_algo}')

        for x in range(MESH_DIM):
            for y in range(MESH_DIM):
                node = x * MESH_DIM + y
                ni = id_map[f'cluster_ni_{x}_{y}']
                noc.o_MAP(node * NODE_SIZE, NODE_SIZE, ni, rm_base=True)
                self.__add_node(test, node, noc.i_NARROW_INPUT(ni),
                    lambda itf, ni=ni: noc.o_NARROW_BIND(itf, ni))

    def __init_graph(self, topology):
        routing = 'shortest_path' if topology == 'graph_tree' else 'up_down'
        noc = FlooNocV2Graph(self, 'noc', narrow_width=NARROW_WIDTH, wide_width=WIDE_WIDTH,
            ni_outstanding_reqs=32, router_input_queue_size=2, routing=routing)

        # NIs are nodes 100+n, routers 0..
        nis = [100 + node for node in range(GRAPH_NODES)]
        if topology == 'graph_tree':
            noc.add_router(0, name='root')
            for leaf in range(3):
                noc.add_router(1 + leaf, name=f'leaf_{leaf}')
                noc.add_link(0, 1 + leaf)
            for node in range(GRAPH_NODES):
                noc.add_network_interface(nis[node])
                noc.add_link(1 + node // 2, nis[node])
        else:
            stages = RING_STAGES if topology == 'graph_ring_stages' else 0
            for node in range(GRAPH_NODES):
                noc.add_router(node)
                noc.add_network_interface(nis[node])
                noc.add_link(node, nis[node])
            for node in range(GRAPH_NODES):
                noc.add_link(node, (node + 1) % GRAPH_NODES, stages=stages)

        test = FloonocCalibTest(self, 'test', GRAPH_NODES, NODE_SIZE, 0, topology)

        for node in range(GRAPH_NODES):
            noc.o_MAP(node * NODE_SIZE, NODE_SIZE, nis[node], rm_base=True)
            self.__add_node(test, node, noc.i_NARROW_INPUT(nis[node]),
                lambda itf, ni=nis[node]: noc.o_NARROW_BIND(itf, ni))

    def __init_2nodes(self, topology):
        use_router = topology == 'router'
        positions = [(0, 0), (2, 0)] if use_router else [(0, 0), (1, 0)]
        dim_x = positions[1][0] + 1

        # Same full memory map in every NI: each node's range points to its
        # mesh position, with the base removed so receivers see zero-based
        # addresses (the fabric's o_MAP(rm_base=True) behavior).
        nis = []
        for node in range(2):
            x, y = positions[node]
            config = FloonocNetworkInterfaceV2Config(node_id=xy_node_id(x, y),
                narrow_width=NARROW_WIDTH, wide_width=WIDE_WIDTH,
                ni_outstanding_reqs=32)
            for map_node in range(2):
                map_x, map_y = positions[map_node]
                config.add_mapping(f'node{map_node}', base=map_x * NODE_SIZE,
                    size=NODE_SIZE, node_id=xy_node_id(map_x, map_y),
                    remove_offset=map_x * NODE_SIZE)
            nis.append(FloonocNetworkInterfaceV2(self, f'ni{node}', config=config))

        if use_router:
            # One router per physical network at (1, 0), NI0 on its left
            # port, NI1 on its right port. queue_size matches the RTL
            # ChannelFifoDepth.
            for nw, nw_name in enumerate(['req', 'rsp', 'wide']):
                router = FloonocRouterV2(self, f'{nw_name}_router_1_0',
                    config=FloonocRouterV2Config.xy(1, 0, dim_x, 1, queue_size=2))
                nis[0].o_LINK(nw, router.i_INPUT(DIR_LEFT))
                router.o_OUTPUT(DIR_LEFT, nis[0].i_LINK(nw))
                nis[1].o_LINK(nw, router.i_INPUT(DIR_RIGHT))
                router.o_OUTPUT(DIR_RIGHT, nis[1].i_LINK(nw))
        else:
            # Direct NI-to-NI links (req/rsp/wide), like the RTL bench's
            # cross-connected chimneys.
            for nw in range(3):
                nis[0].o_LINK(nw, nis[1].i_LINK(nw))
                nis[1].o_LINK(nw, nis[0].i_LINK(nw))

        test = FloonocCalibTest(self, 'test', 2, NODE_SIZE,
            positions[1][0] * NODE_SIZE, topology)

        for node in range(2):
            self.__add_node(test, node, nis[node].i_NARROW_INPUT(),
                nis[node].o_NARROW_OUTPUT)


class Chip(gvsoc.systree.Component):

    def __init__(self, parent, name=None):
        super().__init__(parent, name)

        topology = TargetParameter(
            self, name='topology', value='direct',
            description='NoC topology (direct, router or mesh)', cast=str
        ).get_value()

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=100000000)
        soc = Testbench(self, 'soc', topology=topology)
        clock.o_CLOCK(soc.i_CLOCK())


class Target(gvsoc.runner.Target):

    gapy_description = "Floonoc calibration test (RTL testbench mirrors)"
    model = Chip
    name = "test"
