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
# Authors: Germain Haugou (germain.haugou@gmail.com)
#          Routing algorithms ported from FlooNoC-Flex (pulp/floonoc_flex).
#

"""Routing-table generation for FlooNoC v2 ID-table routing.

Pure Python, independent of the model: every generator takes a
:class:`Topology` and returns next-hop tables
``{router: {dest_ni: next_node}}``, where ``next_node`` is a neighbour router,
or the destination NI itself once its router is reached. The graph fabric
(``FlooNocV2GraphFabric``) converts them into per-router output ports.

Only NIs are destinations: memory-map entries point to NIs, and the
responses travel back to the source NI.

:func:`check_routes` walks every (source NI, destination NI) route, rejects
unreachable destinations and routing loops, and checks deadlock freedom:
routers use wormhole arbitration with a single virtual channel, like the
RTL, so the channel dependency graph built from the routes must be acyclic.

Generators:

- ``shortest_path``: BFS shortest paths (ties broken by link order). Not
  deadlock-free on topologies with cycles (rings, tori).
- ``up_down``: Up*/Down* routing on a BFS spanning tree. Deadlock-free on
  any topology, at the cost of non-minimal routes.
- ``multi_tree``: Up*/Down* over several spanning trees, each destination
  routed on the tree whose root is closest. Spreads the load better than a
  single tree (the FlooNoC-Flex folded hexatorus routing), but mixing trees
  is only deadlock-free with one virtual channel per tree, which the routers
  do not have: check_routes rejects it when the trees combine into a cycle.
- ``dimension_order``: XY/YX(/XYZ) routing from router coordinates.
- ``hexamesh``: axial dimension-order routing from hexagonal router
  coordinates.
- ``next_hops``: explicit next-hop table (e.g. loaded from a routing.yml).
"""

from __future__ import annotations

from collections import deque


class RoutingError(RuntimeError):
    pass


class Topology:
    """Routing view of a FlooNoC v2 network.

    Nodes are integer IDs shared by routers and NIs. Links are bidirectional
    and kept in insertion order, which also fixes the port order of the
    routers and the tie-breaking of the generators.
    """

    def __init__(self):
        self.routers: list[int] = []
        self.nis: list[int] = []
        self._router_set: set[int] = set()
        self._ni_set: set[int] = set()
        self.names: dict[int, str] = {}
        # Router coordinates, used by the coordinate-based generators.
        self.coords: dict[int, tuple] = {}
        # Neighbours of each node, in link order.
        self.adj: dict[int, list[int]] = {}

    def add_router(self, node_id: int, name: str, coord: tuple | None=None):
        self._add_node(node_id, name)
        self.routers.append(node_id)
        self._router_set.add(node_id)
        if coord is not None:
            self.coords[node_id] = tuple(coord)

    def add_ni(self, node_id: int, name: str):
        self._add_node(node_id, name)
        self.nis.append(node_id)
        self._ni_set.add(node_id)

    def _add_node(self, node_id: int, name: str):
        if node_id in self.adj:
            raise RoutingError(f'Node {node_id} ({name}) declared twice')
        if node_id < 0:
            raise RoutingError(f'Node {name} has a negative ID ({node_id})')
        self.names[node_id] = name
        self.adj[node_id] = []

    def add_link(self, a: int, b: int):
        for node in (a, b):
            if node not in self.adj:
                raise RoutingError(f'Link {a} <-> {b}: unknown node {node}')
        if a == b:
            raise RoutingError(f'Link from node {self.name(a)} to itself')
        if b in self.adj[a]:
            raise RoutingError(f'Duplicate link between {self.name(a)} and {self.name(b)}')
        if self.is_ni(a) and self.is_ni(b):
            raise RoutingError(f'NIs {self.name(a)} and {self.name(b)} cannot be linked '
                'together, an NI connects to one router')
        self.adj[a].append(b)
        self.adj[b].append(a)

    def set_port_order(self, node: int, neighbours: list[int]):
        """Reorder the links of a node, i.e. the port order of a router.

        The port order is the round-robin arbitration order of the router, so
        a model mirroring an RTL router should use the RTL port order.
        neighbours must be a permutation of the node's neighbours.
        """
        if sorted(neighbours) != sorted(self.adj[node]):
            raise RoutingError(f'Port order of {self.name(node)} must list its neighbours '
                f'{[self.name(n) for n in self.adj[node]]}')
        self.adj[node] = list(neighbours)

    def is_router(self, node: int) -> bool:
        return node in self._router_set

    def is_ni(self, node: int) -> bool:
        return node in self._ni_set

    def name(self, node: int) -> str:
        return self.names.get(node, str(node))

    def ni_router(self, ni: int) -> int:
        """The router an NI is attached to."""
        links = self.adj[ni]
        if len(links) != 1:
            raise RoutingError(f'NI {self.name(ni)} must have exactly one link to a router, '
                f'it has {len(links)}')
        return links[0]

    def router_adj(self, router: int) -> list[int]:
        """Router neighbours of a router, in link order."""
        return [n for n in self.adj[router] if n in self._router_set]

    def check(self):
        """Check the structure: every NI hangs off exactly one router."""
        for ni in self.nis:
            self.ni_router(ni)


def _tables_from_first_hops(topo: Topology, first_hops: dict[int, dict[int, int]]) \
        -> dict[int, dict[int, int]]:
    """Build next-hop tables from per-router router-to-router first hops.

    first_hops[src][target_router] gives the neighbour router a packet
    leaving src towards target_router goes to.
    """
    tables = {}
    for src in topo.routers:
        table = {}
        for ni in topo.nis:
            target = topo.ni_router(ni)
            if target == src:
                table[ni] = ni
            elif target in first_hops[src]:
                table[ni] = first_hops[src][target]
            else:
                raise RoutingError(f'Router {topo.name(src)} cannot reach NI '
                    f'{topo.name(ni)} (router {topo.name(target)})')
        tables[src] = table
    return tables


def shortest_path(topo: Topology) -> dict[int, dict[int, int]]:
    first_hops = {}
    for src in topo.routers:
        hops = {}
        queue = deque()
        for neighbour in topo.router_adj(src):
            if neighbour not in hops:
                hops[neighbour] = neighbour
                queue.append(neighbour)
        while queue:
            curr = queue.popleft()
            for neighbour in topo.router_adj(curr):
                if neighbour != src and neighbour not in hops:
                    hops[neighbour] = hops[curr]
                    queue.append(neighbour)
        first_hops[src] = hops
    return _tables_from_first_hops(topo, first_hops)


def _bfs_levels(topo: Topology, root: int) -> dict[int, int]:
    levels = {root: 0}
    queue = deque([root])
    while queue:
        curr = queue.popleft()
        for neighbour in topo.router_adj(curr):
            if neighbour not in levels:
                levels[neighbour] = levels[curr] + 1
                queue.append(neighbour)
    if len(levels) != len(topo.routers):
        missing = [topo.name(r) for r in topo.routers if r not in levels]
        raise RoutingError(f'Routers not connected to the rest of the network: {missing}')
    return levels


def _up_down_first_hops(topo: Topology, levels: dict[int, int]) -> dict[int, dict[int, int]]:
    """First hops of Up*/Down* routing: a route never goes up after going down.

    A link is "up" towards the root (lower level, ties broken by node ID).
    Shortest legal route, found by BFS over (node, has_gone_down) states.
    """
    def is_up(u, v):
        return levels[v] < levels[u] or (levels[v] == levels[u] and v < u)

    first_hops = {}
    for src in topo.routers:
        hops = {}
        visited = {(src, False)}
        queue = deque([(src, False, None)])
        while queue:
            curr, gone_down, first_hop = queue.popleft()
            if curr != src and curr not in hops:
                hops[curr] = first_hop
            for neighbour in topo.router_adj(curr):
                going_up = is_up(curr, neighbour)
                if gone_down and going_up:
                    continue
                state = (neighbour, gone_down or not going_up)
                if state not in visited:
                    visited.add(state)
                    queue.append((neighbour, state[1],
                        neighbour if first_hop is None else first_hop))
        first_hops[src] = hops
    return first_hops


def up_down(topo: Topology, root: int | None=None) -> dict[int, dict[int, int]]:
    if root is None:
        root = topo.routers[len(topo.routers) // 2]
    return _tables_from_first_hops(topo, _up_down_first_hops(topo, _bfs_levels(topo, root)))


def multi_tree(topo: Topology, nb_trees: int=4) -> dict[int, dict[int, int]]:
    routers = sorted(topo.routers)
    step = max(1, len(routers) // nb_trees)
    roots = [routers[(i * step) % len(routers)] for i in range(nb_trees)]
    trees = [_bfs_levels(topo, root) for root in roots]
    tree_hops = [_up_down_first_hops(topo, levels) for levels in trees]

    # Each destination router is routed on the tree whose root is closest.
    dest_tree = {}
    for router in routers:
        dest_tree[router] = min(range(nb_trees), key=lambda t: (trees[t][router], t))

    first_hops = {}
    for src in topo.routers:
        first_hops[src] = {target: tree_hops[dest_tree[target]][src][target]
            for target in topo.routers
            if target != src and target in tree_hops[dest_tree[target]][src]}
    return _tables_from_first_hops(topo, first_hops)


def dimension_order(topo: Topology, order: str='xy') -> dict[int, dict[int, int]]:
    """Dimension-order routing from router coordinates.

    order lists the axes in routing order ('xy', 'yx', 'xyz', ...); a route
    corrects the first axis, then the next one. Every router needs a
    coordinate with one entry per axis, and the next router along an axis
    must exist.
    """
    axes = ['xyz'.index(axis) for axis in order]
    coord_to_router = {}
    for router in topo.routers:
        if router not in topo.coords:
            raise RoutingError(f'Router {topo.name(router)} has no coordinates, needed by '
                f'{order.upper()} routing')
        coord_to_router[topo.coords[router]] = router

    first_hops = {}
    for src in topo.routers:
        src_coord = topo.coords[src]
        hops = {}
        for target in topo.routers:
            if target == src:
                continue
            dst_coord = topo.coords[target]
            next_coord = list(src_coord)
            for axis in axes:
                if src_coord[axis] != dst_coord[axis]:
                    next_coord[axis] += 1 if dst_coord[axis] > src_coord[axis] else -1
                    break
            next_router = coord_to_router.get(tuple(next_coord))
            if next_router is None or next_router not in topo.router_adj(src):
                raise RoutingError(f'{order.upper()} routing from {topo.name(src)} to '
                    f'{topo.name(target)}: no link to position {tuple(next_coord)}')
            hops[target] = next_router
        first_hops[src] = hops
    return _tables_from_first_hops(topo, first_hops)


def hexamesh(topo: Topology) -> dict[int, dict[int, int]]:
    """Axial dimension-order routing on a HexaMesh.

    Routers need axial (q, r) coordinates. Corrects q first, then r; when the
    next position does not exist (irregular border) takes the neighbour
    closest in hex distance.
    """
    def distance(a, b):
        return (abs(a[0] - b[0]) + abs(a[0] + a[1] - b[0] - b[1]) + abs(a[1] - b[1])) // 2

    coord_to_router = {}
    for router in topo.routers:
        if router not in topo.coords:
            raise RoutingError(f'Router {topo.name(router)} has no axial coordinates, '
                'needed by hexamesh routing')
        coord_to_router[topo.coords[router]] = router

    first_hops = {}
    for src in topo.routers:
        q, r = topo.coords[src]
        hops = {}
        for target in topo.routers:
            if target == src:
                continue
            dst = topo.coords[target]
            if q != dst[0]:
                next_coord = (q + (1 if dst[0] > q else -1), r)
            else:
                next_coord = (q, r + (1 if dst[1] > r else -1))
            next_router = coord_to_router.get(next_coord)
            if next_router is None:
                candidates = [n for n in topo.router_adj(src)]
                next_router = min(candidates,
                    key=lambda n: (distance(topo.coords[n], dst), candidates.index(n)))
            hops[target] = next_router
        first_hops[src] = hops
    return _tables_from_first_hops(topo, first_hops)


def hexamesh_coords(nb_routers: int) -> list[tuple[int, int]]:
    """Axial coordinates of a HexaMesh, spiralling out from the center."""
    ring_walk = [(-1, 1), (-1, 0), (0, -1), (1, -1), (1, 0), (0, 1)]
    coords = [(0, 0)]
    ring = 1
    while len(coords) < nb_routers:
        q, r = ring, 0
        for dq, dr in ring_walk:
            for _ in range(ring):
                if len(coords) < nb_routers:
                    coords.append((q, r))
                q += dq
                r += dr
        ring += 1
    return coords


def next_hops(topo: Topology, table: dict) -> dict[int, dict[int, int]]:
    """Explicit next-hop table {router: {dest: next_node}}, by ID or name.

    The destination may be an NI, or a router (then it stands for every NI
    attached to that router). Every (router, NI) pair must end up routed.
    """
    by_name = {name: node for node, name in topo.names.items()}

    def resolve(key, what):
        if isinstance(key, int) and key in topo.names:
            return key
        if key in by_name:
            return by_name[key]
        raise RoutingError(f'Routing table: unknown {what} {key!r}')

    explicit = {}
    for src_key, routes in table.items():
        src = resolve(src_key, 'router')
        for dst_key, hop_key in routes.items():
            explicit.setdefault(src, {})[resolve(dst_key, 'destination')] = \
                resolve(hop_key, 'next hop')

    tables = {}
    for src in topo.routers:
        routes = explicit.get(src, {})
        result = {}
        for ni in topo.nis:
            target = topo.ni_router(ni)
            if target == src:
                result[ni] = ni
            elif ni in routes:
                result[ni] = routes[ni]
            elif target in routes:
                result[ni] = routes[target]
            else:
                raise RoutingError(f'Routing table: no route from {topo.name(src)} to '
                    f'{topo.name(ni)}')
        tables[src] = result
    return tables


GENERATORS = {
    'shortest_path': shortest_path,
    'up_down': up_down,
    'multi_tree': multi_tree,
    'dimension_order': dimension_order,
    'hexamesh': hexamesh,
}


def check_routes(topo: Topology, tables: dict[int, dict[int, int]],
        allow_deadlock: bool=False) -> list[list[int]]:
    """Validate next-hop tables and return every NI-to-NI route.

    Raises RoutingError on a missing route, a hop through a non-neighbour, a
    routing loop, or (unless allow_deadlock) a cycle in the channel
    dependency graph, which would let wormhole packets deadlock.
    """
    routes = []
    # Channel dependency graph: channel (u, v) -> channels a packet holding
    # it may wait for next.
    deps: dict[tuple, set] = {}

    for src in topo.nis:
        for dst in topo.nis:
            if src == dst:
                continue
            path = [src, topo.ni_router(src)]
            while path[-1] != dst:
                curr = path[-1]
                if not topo.is_router(curr):
                    raise RoutingError(f'Route {topo.name(src)} -> {topo.name(dst)} '
                        f'reaches NI {topo.name(curr)}')
                nxt = tables.get(curr, {}).get(dst)
                if nxt is None:
                    raise RoutingError(f'No route from {topo.name(curr)} to {topo.name(dst)}')
                if nxt not in topo.adj[curr]:
                    raise RoutingError(f'Route {topo.name(src)} -> {topo.name(dst)}: '
                        f'{topo.name(curr)} has no link to next hop {topo.name(nxt)}')
                if len(path) > len(topo.adj) + 1:
                    raise RoutingError(f'Routing loop from {topo.name(src)} to '
                        f'{topo.name(dst)}: {[topo.name(n) for n in path]}')
                path.append(nxt)
            routes.append(path)
            for i in range(len(path) - 2):
                deps.setdefault((path[i], path[i+1]), set()).add((path[i+1], path[i+2]))

    if not allow_deadlock:
        cycle = _find_cycle(deps)
        if cycle is not None:
            channels = ' -> '.join(f'{topo.name(u)}>{topo.name(v)}' for u, v in cycle)
            raise RoutingError('Routing tables can deadlock (wormhole routing with one '
                f'virtual channel needs an acyclic channel dependency graph), cycle: '
                f'{channels}. Use a deadlock-free algorithm (e.g. up_down) or pass '
                'allow_deadlock=True for exploration')
    return routes


def _find_cycle(deps: dict[tuple, set]) -> list | None:
    """Return one cycle of the directed graph deps, or None."""
    WHITE, GREY, BLACK = 0, 1, 2
    color = {}
    parent = {}
    for start in deps:
        if color.get(start, WHITE) != WHITE:
            continue
        stack = [(start, iter(sorted(deps.get(start, ()))))]
        color[start] = GREY
        while stack:
            node, children = stack[-1]
            child = next(children, None)
            if child is None:
                color[node] = BLACK
                stack.pop()
                continue
            state = color.get(child, WHITE)
            if state == GREY:
                cycle = [child]
                curr = node
                while curr != child:
                    cycle.append(curr)
                    curr = parent[curr]
                cycle.reverse()
                return [cycle[-1]] + cycle[:-1]
            if state == WHITE:
                parent[child] = node
                color[child] = GREY
                stack.append((child, iter(sorted(deps.get(child, ())))))
    return None
