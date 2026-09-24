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

"""Unit tests of the FlooNoC v2 routing-table generation and FlooGen loader."""

import os
import sys
import unittest

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_THIS_DIR, '..', '..', 'pulp', 'floonoc_v2'))

import floonoc_v2_routing as R
import floonoc_v2_floogen

_FLOOGEN_DIR = os.path.join(_THIS_DIR, '..', 'floonoc_calib', 'floogen')


def ring(nb_routers):
    topo = R.Topology()
    for i in range(nb_routers):
        topo.add_router(i, f'r{i}')
        topo.add_ni(100 + i, f'ni{i}')
        topo.add_link(i, 100 + i)
    for i in range(nb_routers):
        topo.add_link(i, (i + 1) % nb_routers)
    return topo


def mesh(dim):
    topo = R.Topology()
    for x in range(dim):
        for y in range(dim):
            topo.add_router(x * dim + y, f'r{x}_{y}', (x, y))
            topo.add_ni(100 + x * dim + y, f'ni{x}_{y}')
            topo.add_link(x * dim + y, 100 + x * dim + y)
    for x in range(dim):
        for y in range(dim):
            if x + 1 < dim:
                topo.add_link(x * dim + y, (x + 1) * dim + y)
            if y + 1 < dim:
                topo.add_link(x * dim + y, x * dim + y + 1)
    return topo


class StubNoc:
    """Stand-in for FlooNocV2Graph, recording what load_floogen builds."""

    class Fabric:
        routing = None
        routing_args = {}

    def __init__(self):
        self.topo = R.Topology()
        self.fabric = StubNoc.Fabric()
        self.stages = {}

    def add_router(self, node_id, name=None, coord=None):
        self.topo.add_router(node_id, name, coord)

    def add_network_interface(self, node_id, name=None):
        self.topo.add_ni(node_id, name)

    def add_link(self, node_a, node_b, stages=0, stages_back=None):
        self.topo.add_link(node_a, node_b)
        self.stages[(node_a, node_b)] = stages
        self.stages[(node_b, node_a)] = stages if stages_back is None else stages_back

    def set_port_order(self, node_id, neighbours):
        self.topo.set_port_order(node_id, neighbours)

    def tables(self):
        if isinstance(self.fabric.routing, str):
            return R.GENERATORS[self.fabric.routing](self.topo, **self.fabric.routing_args)
        return R.next_hops(self.topo, self.fabric.routing)


class TestRouting(unittest.TestCase):

    def test_ring_shortest_path_deadlocks(self):
        topo = ring(6)
        with self.assertRaisesRegex(R.RoutingError, 'can deadlock'):
            R.check_routes(topo, R.shortest_path(topo))
        # Accepted on explicit request, for exploration.
        R.check_routes(topo, R.shortest_path(topo), allow_deadlock=True)

    def test_ring_up_down(self):
        topo = ring(6)
        routes = R.check_routes(topo, R.up_down(topo))
        self.assertEqual(len(routes), 6 * 5)

    def test_multi_tree_mixing_deadlocks(self):
        topo = ring(6)
        with self.assertRaisesRegex(R.RoutingError, 'can deadlock'):
            R.check_routes(topo, R.multi_tree(topo, nb_trees=2))

    def test_mesh(self):
        topo = mesh(4)
        for algo, args in [('dimension_order', {}), ('dimension_order', {'order': 'yx'}),
                ('shortest_path', {}), ('up_down', {})]:
            routes = R.check_routes(topo, R.GENERATORS[algo](topo, **args))
            self.assertEqual(len(routes), 16 * 15)
        # XY: minimal routes, corner to corner is 7 routers.
        routes = R.check_routes(topo, R.dimension_order(topo))
        self.assertEqual(max(len(route) for route in routes), 7 + 2)

    def test_hexamesh(self):
        topo = R.Topology()
        coords = R.hexamesh_coords(7)
        index = {coord: i for i, coord in enumerate(coords)}
        for i, coord in enumerate(coords):
            topo.add_router(i, f'h{i}', coord)
            topo.add_ni(100 + i, f'n{i}')
            topo.add_link(i, 100 + i)
        for i, (q, r) in enumerate(coords):
            for dq, dr in [(1, 0), (0, 1), (-1, 1)]:
                if (q + dq, r + dr) in index:
                    topo.add_link(i, index[(q + dq, r + dr)])
        R.check_routes(topo, R.hexamesh(topo))

    def test_explicit_table(self):
        topo = ring(3)
        with self.assertRaisesRegex(R.RoutingError, 'no route'):
            R.next_hops(topo, {})
        # A destination router stands for its NIs.
        table = {'r0': {'r1': 'r1', 'r2': 'r2'}, 'r1': {'r0': 'r0', 'r2': 'r2'},
            'r2': {'r0': 'r0', 'r1': 'r1'}}
        R.check_routes(topo, R.next_hops(topo, table), allow_deadlock=True)

    def test_loop(self):
        topo = ring(4)
        table = {'r0': {'ni2': 'r1'}, 'r1': {'ni2': 'r0'}}
        for router in ['r0', 'r1', 'r2', 'r3']:
            for ni in ['ni0', 'ni1', 'ni2', 'ni3']:
                table.setdefault(router, {}).setdefault(ni, 'r1' if router == 'r0' else 'r0')
        tables = R.next_hops(topo, table)
        with self.assertRaisesRegex(R.RoutingError, 'loop|no link'):
            R.check_routes(topo, tables, allow_deadlock=True)

    def test_structure(self):
        topo = R.Topology()
        topo.add_router(0, 'r0')
        topo.add_ni(1, 'ni1')
        with self.assertRaisesRegex(R.RoutingError, 'exactly one link'):
            topo.check()
        topo.add_ni(2, 'ni2')
        with self.assertRaisesRegex(R.RoutingError, 'cannot be linked'):
            topo.add_link(1, 2)


class TestFloogen(unittest.TestCase):

    def load(self, name, **kwargs):
        noc = StubNoc()
        id_map = floonoc_v2_floogen.load_floogen(noc, os.path.join(_FLOOGEN_DIR, name),
            **kwargs)
        return noc, id_map

    def test_mesh_xy_and_id(self):
        # The ID tables FlooGen generates for the RTL mesh are shortest paths
        # (ties go to the first port, North, so they are not XY), and
        # deadlock-free like the XY routes.
        noc_xy, id_map = self.load('axi_mesh_xy.yml')
        noc_id, _ = self.load('axi_mesh_id.yml')
        self.assertEqual(len(noc_xy.topo.routers), 16)
        self.assertEqual(len(noc_xy.topo.nis), 20)
        routes_xy = R.check_routes(noc_xy.topo, noc_xy.tables())
        routes_id = R.check_routes(noc_id.topo, noc_id.tables())
        self.assertEqual([len(route) for route in routes_xy],
            [len(route) for route in routes_id])
        self.assertNotEqual(routes_xy, routes_id)
        # Routers first, then NIs, in FlooGen order.
        self.assertEqual(id_map['router_0_0'], 0)
        self.assertEqual(id_map['cluster_ni_0_0'], 16)

    def test_port_order(self):
        # Router ports follow the FlooGen (RTL) order: N, E, S, W, eject.
        noc, id_map = self.load('axi_mesh_xy.yml')
        names = [noc.topo.name(n) for n in noc.topo.adj[id_map['router_1_1']]]
        self.assertEqual(names, ['router_1_2', 'router_2_1', 'router_1_0', 'router_0_1',
            'cluster_ni_1_1'])

    def test_link_latencies(self):
        import tempfile
        with tempfile.NamedTemporaryFile('w', suffix='.yml', delete=False) as file:
            file.write('router_0_0:\n  router_0_1: 3\n')
        try:
            noc, id_map = self.load('axi_mesh_xy.yml', link_latencies_path=file.name,
                default_link_latency=2)
        finally:
            os.unlink(file.name)
        r00, r01, r10 = id_map['router_0_0'], id_map['router_0_1'], id_map['router_1_0']
        # Listed link, both directions: latency 3 = 2 stages.
        self.assertEqual(noc.stages[(r00, r01)], 2)
        self.assertEqual(noc.stages[(r01, r00)], 2)
        # Others take the default latency 2 = 1 stage.
        self.assertEqual(noc.stages[(r00, r10)], 1)


if __name__ == '__main__':
    unittest.main()
