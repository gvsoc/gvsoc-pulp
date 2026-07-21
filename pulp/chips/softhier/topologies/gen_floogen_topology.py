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

"""
Generates a topology's floogen.yml + routing.yml from the SoftHierArch
parameters. Node naming convention:
    router_{cluster_id}      -- router colocated with a cluster
    cluster_{cluster_id}_ni  -- network interface for that cluster 
Extra non-cluster routers (e.g. hierarchical_ring's global routers) are
named router_global_{g}.

Router/endpoint declaration order in the emitted YAML is load-bearing:
floogen preserves YAML list order through to its graph traversal order.
floonoc_flex.py's load_from_floogen() assigns runtime node ids by that
same traversal order (all routers first, then all NIs). The routing-table
precomputation below replicates that exact id assignment so the routing
tables it computes match what the real loader will independently arrive
at when it later loads this same YAML.
"""

import argparse
import os
import sys
from pathlib import Path

import yaml

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from floogen.config_parser import parse_config
from floogen.model.network import Network

_this_dir = os.path.dirname(os.path.abspath(__file__))
_arch_base_path = os.path.normpath(os.path.join(_this_dir, '..', 'softhier_arch_base.py'))
import importlib.util
_spec = importlib.util.spec_from_file_location('softhier_arch_base', _arch_base_path)
_arch_base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_arch_base)


# Shared protocol/network_type to satisfy floogen's own schema validation.
_PROTOCOLS = [
    {"name": "narrow_in", "type": "narrow", "protocol": "AXI4",
     "data_width": 64, "addr_width": 48, "id_width": 4, "user_width": 1},
    {"name": "narrow_out", "type": "narrow", "protocol": "AXI4",
     "data_width": 64, "addr_width": 48, "id_width": 2, "user_width": 1},
    {"name": "wide_in", "type": "wide", "protocol": "AXI4",
     "data_width": 512, "addr_width": 48, "id_width": 3, "user_width": 1},
    {"name": "wide_out", "type": "wide", "protocol": "AXI4",
     "data_width": 512, "addr_width": 48, "id_width": 1, "user_width": 1},
]


def _ni_endpoint(name):
    # floogen unconditionally appends "_ni", so this endpoint is declared
    # under its bare name (e.g. "cluster_3" or "cluster_1_2"), and the
    # node name that ends up in load_from_floogen()'s node_to_id/id_map
    # is "<name>_ni" (see ni_graph_name).
    return {
        "name": name,
        "mgr_port_protocol": ["narrow_in", "wide_in"],
    }


def ni_graph_name(name):
    return f"{name}_ni"


def generate_ring(arch, out_dir, topology_name):
    """
    Flat ring: N routers in a closed loop, one NI per router.

        for i in range(arch.num_cluster):
            noc.add_router(i, num_queues=3)
            noc.add_network_interface(arch.num_cluster + i)
        for i in range(arch.num_cluster):
            noc.add_link(arch.num_cluster + i, i, latency=1)          # NI <-> router
        for i in range(arch.num_cluster):
            noc.add_link(i, (i + 1) % arch.num_cluster, latency=1)    # router <-> router ring
        noc.generate_routing_tables_shortest_path()
    """
    n = arch.num_cluster
    router_names = [f"router_{i}" for i in range(n)]
    ni_endpoint_names = [f"cluster_{i}" for i in range(n)]           # bare, for YAML endpoints/connections
    ni_names = [ni_graph_name(name) for name in ni_endpoint_names]   # cluster_{i}_ni, real graph/id_map name

    routers_yaml = [{"name": name, "degree": 3} for name in router_names]
    endpoints_yaml = [_ni_endpoint(name) for name in ni_endpoint_names]

    # Router declaration comes first in the YAML (see module docstring on
    # why this order matters): id_map will assign routers 0..n-1, then
    # NIs n..2n-1, matching the original code's r_id=i / ni_id=n+i scheme.
    name_to_id = {name: i for i, name in enumerate(router_names)}
    name_to_id.update({name: n + i for i, name in enumerate(ni_names)})
    id_to_name = {v: k for k, v in name_to_id.items()}
    nb_nodes = 2 * n

    # Links for routing-table computation use the real graph names
    # (ni_names, "_ni"-suffixed); the YAML connections instead uses the bare 
    # endpoint names.
    links = []
    for i in range(n):
        links.append((ni_names[i], router_names[i], arch.link_latency))
    for i in range(n):
        links.append((router_names[i], router_names[(i + 1) % n], arch.link_latency))

    connections_yaml = []
    for i in range(n):
        connections_yaml.append({"src": ni_endpoint_names[i], "dst": router_names[i]})
    for i in range(n):
        connections_yaml.append({"src": router_names[i], "dst": router_names[(i + 1) % n]})

    floogen_doc = {
        "name": topology_name,
        "description": f"Generated FlooGen topology for SoftHier {topology_name}",
        "network_type": "narrow-wide",
        "routing": {"route_algo": "ID", "use_id_table": True},
        "protocols": _PROTOCOLS,
        "endpoints": endpoints_yaml,
        "routers": routers_yaml,
        "connections": connections_yaml,
    }

    routing_tables = _compute_ring_routing_tables(
        router_names, ni_names, links, nb_nodes, name_to_id, id_to_name)

    _write_and_validate(out_dir, topology_name, floogen_doc, routing_tables, links)


def _compute_ring_routing_tables(router_names, ni_names, links, nb_nodes, name_to_id, id_to_name):
    """
    Computes ring routing tables (shortest direction around the ring,
    wraparound-aware).
    Returns the name-based routing table format expected by
    FlooNoc.load_custom_routing_tables() (routing.yml with
    route_algo: "ID"): {src_name: {dst_name: next_hop_name}}.
    """
    routers = sorted(name_to_id[n] for n in router_names)
    nis = [name_to_id[n] for n in ni_names]

    routing_tables = {r: {d: -1 for d in range(nb_nodes)} for r in routers}

    ni_to_router = {}
    for src_name, dst_name, _latency in links:
        a, b = name_to_id[src_name], name_to_id[dst_name]
        if a in nis and b in routers:
            ni_to_router[a] = b
        elif b in nis and a in routers:
            ni_to_router[b] = a

    for src in routers:
        src_idx = routers.index(src)
        for dst in range(nb_nodes):
            target_router = dst if dst in routers else ni_to_router.get(dst, -1)
            if target_router == -1:
                continue
            if src == target_router:
                routing_tables[src][dst] = dst
                continue
            target_idx = routers.index(target_router)
            # Go whichever direction around the ring is fewer hops (see the
            # matching fix in FlooNoc.generate_routing_tables_ring()).
            num_routers = len(routers)
            forward_dist = (target_idx - src_idx) % num_routers
            backward_dist = num_routers - forward_dist
            if forward_dist <= backward_dist:
                next_hop_idx = (src_idx + 1) % num_routers
            else:
                next_hop_idx = (src_idx - 1) % num_routers
            routing_tables[src][dst] = routers[next_hop_idx]

    # Translate to the name-based format load_custom_routing_tables()
    # expects, and check for unresolved (-1) entries along the way --
    # for a fully-specified ring every id 0..nb_nodes-1 is either a
    # router or an NI with a mapped router, so none should remain.
    missing = []
    named_tables = {}
    for src_id, row in routing_tables.items():
        named_tables[id_to_name[src_id]] = {}
        for dst_id, next_hop_id in row.items():
            if next_hop_id == -1:
                missing.append((id_to_name[src_id], id_to_name[dst_id]))
                continue
            named_tables[id_to_name[src_id]][id_to_name[dst_id]] = id_to_name[next_hop_id]
    if missing:
        raise RuntimeError(
            f"Ring routing table generation left {len(missing)} unresolved "
            f"(-1) entries, e.g. {missing[:5]}")

    return named_tables


def generate_hier_ring(arch, out_dir, topology_name):
    """
    Hierarchical ring: G local rings of L clusters each, joined by one
    global ring connecting one "bridge" router per local ring (local
    index 0). 

        for g in range(G):
            gr_id = N + g
            noc.add_router(gr_id, num_queues=3)
            for l in range(L):
                cluster_id = g * L + l
                lr_id = cluster_id
                ni_id = N + G + cluster_id
                if l == 0:
                    noc.add_router(lr_id, num_queues=4)
                    noc.add_link(lr_id, gr_id, latency=1)
                else:
                    noc.add_router(lr_id, num_queues=3)
                noc.add_network_interface(ni_id)
                noc.add_link(ni_id, lr_id, latency=1)
                next_lr_id = (g * L) + ((l + 1) % L)
                noc.add_link(lr_id, next_lr_id, latency=1)
            next_gr_id = N + ((g + 1) % G)
            noc.add_link(gr_id, next_gr_id, latency=1)
        noc.generate_routing_tables_shortest_path()

    Local routers are named router_{cluster_id} with cluster_id = g*L+l,
    so iterating g then l already declares them in group-major order
    (matches the module docstring's router_{cluster_id} convention);
    the extra global routers are named router_global_{g}.
    """
    G = arch.num_global_clusters
    L = arch.num_local_clusters
    N = arch.num_cluster
    assert N == G * L, (
        f"num_cluster ({N}) must equal num_global_clusters * num_local_clusters "
        f"({G} * {L} = {G * L})")

    local_router_names = [f"router_{g * L + l}" for g in range(G) for l in range(L)]
    global_router_names = [f"router_global_{g}" for g in range(G)]
    router_names = local_router_names + global_router_names
    ni_endpoint_names = [f"cluster_{i}" for i in range(N)]            # bare, for YAML endpoints/connections
    ni_names = [ni_graph_name(name) for name in ni_endpoint_names]    # cluster_{i}_ni, real graph/id_map name

    # No "degree" set: router in/out port counts are mixed (bridge
    # routers get an extra port for the global-ring link), so let floogen
    # auto-derive each router's degree from its actual connections.
    routers_yaml = [{"name": name} for name in router_names]
    endpoints_yaml = [_ni_endpoint(name) for name in ni_endpoint_names]

    name_to_id = {name: i for i, name in enumerate(router_names)}
    name_to_id.update({name: len(router_names) + i for i, name in enumerate(ni_names)})
    id_to_name = {v: k for k, v in name_to_id.items()}
    nb_nodes = len(router_names) + N

    links = []
    connections_yaml = []
    for g in range(G):
        gr_name = f"router_global_{g}"
        for l in range(L):
            cluster_id = g * L + l
            lr_name = f"router_{cluster_id}"
            if l == 0:
                links.append((lr_name, gr_name, arch.link_latency))
                connections_yaml.append({"src": lr_name, "dst": gr_name})
            links.append((ni_names[cluster_id], lr_name, arch.link_latency))
            connections_yaml.append({"src": ni_endpoint_names[cluster_id], "dst": lr_name})
            next_lr_name = f"router_{g * L + (l + 1) % L}"
            links.append((lr_name, next_lr_name, arch.link_latency))
            connections_yaml.append({"src": lr_name, "dst": next_lr_name})
        next_gr_name = f"router_global_{(g + 1) % G}"
        links.append((gr_name, next_gr_name, arch.link_latency))
        connections_yaml.append({"src": gr_name, "dst": next_gr_name})

    floogen_doc = {
        "name": topology_name,
        "description": f"Generated FlooGen topology for SoftHier {topology_name}",
        "network_type": "narrow-wide",
        "routing": {"route_algo": "ID", "use_id_table": True},
        "protocols": _PROTOCOLS,
        "endpoints": endpoints_yaml,
        "routers": routers_yaml,
        "connections": connections_yaml,
    }

    routing_tables = _compute_shortest_path_routing_tables(
        router_names, ni_names, links, nb_nodes, name_to_id, id_to_name)

    _write_and_validate(out_dir, topology_name, floogen_doc, routing_tables, links)


def _compute_shortest_path_routing_tables(router_names, ni_names, links, nb_nodes, name_to_id, id_to_name):
    """
    Generic BFS shortest-path routing over the router adjacency graph.
    """
    routers = set(name_to_id[n] for n in router_names)
    nis = set(name_to_id[n] for n in ni_names)

    adj = {r: set() for r in routers}
    ni_to_router = {}
    for src_name, dst_name, _latency in links:
        a, b = name_to_id[src_name], name_to_id[dst_name]
        if a in nis and b in routers:
            ni_to_router[a] = b
        elif b in nis and a in routers:
            ni_to_router[b] = a
        elif a in routers and b in routers:
            adj[a].add(b)
            adj[b].add(a)

    routing_tables = {r: {d: -1 for d in range(nb_nodes)} for r in routers}
    for src in routers:
        best_first_hop = {}
        visited = {src}
        queue = list(adj[src])
        for neighbor in queue:
            visited.add(neighbor)
            best_first_hop[neighbor] = neighbor
        head = 0
        while head < len(queue):
            curr = queue[head]
            head += 1
            for neighbor in adj[curr]:
                if neighbor not in visited:
                    visited.add(neighbor)
                    best_first_hop[neighbor] = best_first_hop[curr]
                    queue.append(neighbor)
        for dst in range(nb_nodes):
            target_router = dst if dst in routers else ni_to_router.get(dst, -1)
            if target_router == -1:
                continue
            if src == target_router:
                routing_tables[src][dst] = dst
            elif target_router in best_first_hop:
                routing_tables[src][dst] = best_first_hop[target_router]
            else:
                routing_tables[src][dst] = src

    missing = []
    named_tables = {}
    for src_id, row in routing_tables.items():
        named_tables[id_to_name[src_id]] = {}
        for dst_id, next_hop_id in row.items():
            if next_hop_id == -1:
                missing.append((id_to_name[src_id], id_to_name[dst_id]))
                continue
            named_tables[id_to_name[src_id]][id_to_name[dst_id]] = id_to_name[next_hop_id]
    if missing:
        raise RuntimeError(
            f"Shortest-path routing table generation left {len(missing)} "
            f"unresolved (-1) entries, e.g. {missing[:5]}")
    return named_tables


def generate_mesh2d(arch, out_dir, topology_name):
    """
    2D mesh or 2D torus: a dim_x * dim_y router grid,
    one NI per router, with an optional wraparound link closing each
    row/column into a ring.

        for y in range(dim_y):
            for x in range(dim_x):
                noc.add_router(get_router_id(x, y), num_queues=5)
                noc.add_network_interface(get_ni_id(x, y))
        for y in range(dim_y):
            for x in range(dim_x - 1):
                noc.add_link(r(x, y), r(x + 1, y), latency=1)        # inner row
            if is_torus:
                noc.add_link(r(dim_x - 1, y), r(0, y), latency=5)    # row wraparound
        for x in range(dim_x):
            for y in range(dim_y - 1):
                noc.add_link(r(x, y), r(x, y + 1), latency=1)        # inner column
            if is_torus:
                noc.add_link(r(x, dim_y - 1), r(x, 0), latency=5)    # column wraparound
        for y in range(dim_y):
            for x in range(dim_x):
                noc.add_link(ni(x, y), r(x, y), latency=1)
        noc.generate_routing_tables_shortest_path()

    Router/NI names are coordinate-based (router_{x}_{y}, cluster_{x}_{y}),
    not the flat cluster_{cluster_id} used by the ring family, since this
    shape is inherently 2D.
    """
    dim_x = arch.num_cluster_x
    dim_y = arch.num_cluster_y
    assert dim_x * dim_y == arch.num_cluster, (
        f"num_cluster ({arch.num_cluster}) must equal num_cluster_x * num_cluster_y "
        f"({dim_x} * {dim_y} = {dim_x * dim_y})")
    is_torus = bool(getattr(arch, 'is_torus', False))
    wraparound_latency = arch.wraparound_latency if is_torus else arch.link_latency

    def r_name(x, y):
        return f"router_{x}_{y}"

    def ep_name(x, y):
        return f"cluster_{x}_{y}"

    router_names = [r_name(x, y) for y in range(dim_y) for x in range(dim_x)]
    ni_endpoint_names = [ep_name(x, y) for y in range(dim_y) for x in range(dim_x)]
    ni_names = [ni_graph_name(name) for name in ni_endpoint_names]

    # No "degree" set (see generate_hier_ring): edge/corner routers have
    # fewer neighbors than interior ones, so let floogen auto-derive it.
    routers_yaml = [{"name": name} for name in router_names]
    endpoints_yaml = [_ni_endpoint(name) for name in ni_endpoint_names]

    name_to_id = {name: i for i, name in enumerate(router_names)}
    name_to_id.update({name: len(router_names) + i for i, name in enumerate(ni_names)})
    id_to_name = {v: k for k, v in name_to_id.items()}
    nb_nodes = len(router_names) + len(ni_names)

    links = []
    connections_yaml = []

    def add_router_link(a, b, latency):
        links.append((a, b, latency))
        connections_yaml.append({"src": a, "dst": b})

    for y in range(dim_y):
        for x in range(dim_x - 1):
            add_router_link(r_name(x, y), r_name(x + 1, y), arch.link_latency)
        if is_torus and dim_x > 1:
            add_router_link(r_name(dim_x - 1, y), r_name(0, y), wraparound_latency)
    for x in range(dim_x):
        for y in range(dim_y - 1):
            add_router_link(r_name(x, y), r_name(x, y + 1), arch.link_latency)
        if is_torus and dim_y > 1:
            add_router_link(r_name(x, dim_y - 1), r_name(x, 0), wraparound_latency)
    for y in range(dim_y):
        for x in range(dim_x):
            links.append((ni_graph_name(ep_name(x, y)), r_name(x, y), arch.link_latency))
            connections_yaml.append({"src": ep_name(x, y), "dst": r_name(x, y)})

    floogen_doc = {
        "name": topology_name,
        "description": f"Generated FlooGen topology for SoftHier {topology_name}",
        "network_type": "narrow-wide",
        "routing": {"route_algo": "ID", "use_id_table": True},
        "protocols": _PROTOCOLS,
        "endpoints": endpoints_yaml,
        "routers": routers_yaml,
        "connections": connections_yaml,
    }

    if is_torus:
        routing_tables = _compute_torus_xy_routing_tables(
            router_names, ni_names, links, nb_nodes, name_to_id, id_to_name, dim_x, dim_y)
    else:
        routing_tables = _compute_xy_routing_tables(
            router_names, ni_names, links, nb_nodes, name_to_id, id_to_name, dim_x)

    _write_and_validate(out_dir, topology_name, floogen_doc, routing_tables, links)


def _finalize_routing_tables(routing_tables, id_to_name, algo_name):
    """
    Shared last step for the id-keyed routing_tables dicts the
    dimension-order helpers below build: translate to the name-based
    format load_custom_routing_tables() expects, and fail loudly on any
    unresolved (-1) entry instead of shipping a silently-broken route.
    """
    missing = []
    named_tables = {}
    for src_id, row in routing_tables.items():
        named_tables[id_to_name[src_id]] = {}
        for dst_id, next_hop_id in row.items():
            if next_hop_id == -1:
                missing.append((id_to_name[src_id], id_to_name[dst_id]))
                continue
            named_tables[id_to_name[src_id]][id_to_name[dst_id]] = id_to_name[next_hop_id]
    if missing:
        raise RuntimeError(
            f"{algo_name} routing table generation left {len(missing)} "
            f"unresolved (-1) entries, e.g. {missing[:5]}")
    return named_tables


def _compute_xy_routing_tables(router_names, ni_names, links, nb_nodes, name_to_id, id_to_name, dim_x):
    """
    X-then-Y dimension-order routing for a plain (non-torus) mesh.
    """
    routers = sorted(name_to_id[n] for n in router_names)
    routers_set = set(routers)
    nis = set(name_to_id[n] for n in ni_names)

    routing_tables = {r: {d: -1 for d in range(nb_nodes)} for r in routers}

    ni_to_router = {}
    for src_name, dst_name, _latency in links:
        a, b = name_to_id[src_name], name_to_id[dst_name]
        if a in nis and b in routers_set:
            ni_to_router[a] = b
        elif b in nis and a in routers_set:
            ni_to_router[b] = a

    for src in routers:
        sx, sy = src % dim_x, src // dim_x
        for dst in range(nb_nodes):
            target_router = dst if dst in routers_set else ni_to_router.get(dst, -1)
            if target_router == -1:
                continue
            if src == target_router:
                routing_tables[src][dst] = dst
                continue
            dx, dy = target_router % dim_x, target_router // dim_x
            if sx != dx:
                next_hop = src + 1 if dx > sx else src - 1
            else:
                next_hop = src + dim_x if dy > sy else src - dim_x
            routing_tables[src][dst] = next_hop

    return _finalize_routing_tables(routing_tables, id_to_name, "XY")


def _compute_torus_xy_routing_tables(router_names, ni_names, links, nb_nodes, name_to_id, id_to_name, dim_x, dim_y):
    """
    Deadlock-free XY torus routing ("Algorithm 3" from "Developing
    Deadlock-Free Routing Algorithms in Torus NoC").
    Plain XY routing deadlocks on a torus. The fix is to route by default XY 
    as normal, but special-case the packets whose remaining distance on an axis 
    is more than half that axis's width (delta > w//2 / h//2); for those, the
    *short* way home is through the wraparound edge, so the first hop is
    forced across that edge instead of stepping normally. 
    """
    routers = sorted(name_to_id[n] for n in router_names)
    routers_set = set(routers)
    nis = set(name_to_id[n] for n in ni_names)

    routing_tables = {r: {d: -1 for d in range(nb_nodes)} for r in routers}

    ni_to_router = {}
    for src_name, dst_name, _latency in links:
        a, b = name_to_id[src_name], name_to_id[dst_name]
        if a in nis and b in routers_set:
            ni_to_router[a] = b
        elif b in nis and a in routers_set:
            ni_to_router[b] = a

    min_x, max_x = 0, dim_x - 1
    min_y, max_y = 0, dim_y - 1
    w = max_x - min_x + 1
    h = max_y - min_y + 1

    for src in routers:
        sx, sy = src % dim_x, src // dim_x
        for dst in range(nb_nodes):
            target_router = dst if dst in routers_set else ni_to_router.get(dst, -1)
            if target_router == -1:
                continue
            if src == target_router:
                routing_tables[src][dst] = dst
                continue

            dx, dy = target_router % dim_x, target_router // dim_x
            delta_x = abs(dx - sx)
            delta_y = abs(dy - sy)

            if (dy > sy) and (sx > dx) and (delta_x > w // 2):
                next_sx = min_x if sx == max_x else sx + 1
                next_hop = sy * dim_x + next_sx
            elif (dy > sy) and (dx > sx) and (delta_x > w // 2):
                next_sx = max_x if sx == min_x else sx - 1
                next_hop = sy * dim_x + next_sx
            elif (dx > sx) and (dy > sy) and (delta_y > h // 2):
                next_sy = max_y if sy == min_y else sy - 1
                next_hop = next_sy * dim_x + sx
            elif (sy == max_y) and (dy < sy) and (delta_y > h // 2):
                next_sy = min_y
                next_hop = next_sy * dim_x + sx
            else:
                if sx != dx:
                    if sx == min_x and dx > sx and dy > sy:
                        next_sy = sy + 1
                        next_hop = next_sy * dim_x + sx
                    elif sx == max_x and dx < sx and dy > sy:
                        next_sy = sy + 1
                        next_hop = next_sy * dim_x + sx
                    else:
                        next_sx = sx + 1 if dx > sx else sx - 1
                        next_hop = sy * dim_x + next_sx
                else:
                    next_sy = sy + 1 if dy > sy else sy - 1
                    next_hop = next_sy * dim_x + sx

            routing_tables[src][dst] = next_hop

    return _finalize_routing_tables(routing_tables, id_to_name, "Torus arc-model")


def generate_mesh3d(arch, out_dir, topology_name):
    """
    3D mesh or 3D torus: a dim_x * dim_y * dim_z router grid, one NI per
    router, with an optional wraparound link closing each axis into a ring. 

        for z, y, x in <full grid>:
            noc.add_router(get_router_id(x, y, z), num_queues=7)
            noc.add_network_interface(get_ni_id(x, y, z))   # skipped at the 8 corners
        # X links, then (if torus) X wraparound
        # Y links, then (if torus) Y wraparound
        # Z links, then (if torus) Z wraparound
        # NI <-> router links
        noc.generate_routing_tables_shortest_path()

    Router/NI names are coordinate-based (router_{x}_{y}_{z},
    cluster_{x}_{y}_{z}), matching generate_mesh2d's convention.
    """
    dim_x = arch.num_cluster_x
    dim_y = arch.num_cluster_y
    dim_z = arch.num_cluster_z
    assert dim_x * dim_y * dim_z == arch.num_cluster, (
        f"num_cluster ({arch.num_cluster}) must equal num_cluster_x * num_cluster_y * "
        f"num_cluster_z ({dim_x} * {dim_y} * {dim_z} = {dim_x * dim_y * dim_z})")
    is_torus = bool(getattr(arch, 'is_torus', False))

    def r_name(x, y, z):
        return f"router_{x}_{y}_{z}"

    def ep_name(x, y, z):
        return f"cluster_{x}_{y}_{z}"

    router_names = [r_name(x, y, z)
                     for z in range(dim_z) for y in range(dim_y) for x in range(dim_x)]
    ni_endpoint_names = [ep_name(x, y, z)
                          for z in range(dim_z) for y in range(dim_y) for x in range(dim_x)]
    ni_names = [ni_graph_name(name) for name in ni_endpoint_names]

    # No "degree" set (see generate_hier_ring): edge/corner routers have
    # fewer neighbors than interior ones, so let floogen auto-derive it.
    routers_yaml = [{"name": name} for name in router_names]
    endpoints_yaml = [_ni_endpoint(name) for name in ni_endpoint_names]

    name_to_id = {name: i for i, name in enumerate(router_names)}
    name_to_id.update({name: len(router_names) + i for i, name in enumerate(ni_names)})
    id_to_name = {v: k for k, v in name_to_id.items()}
    nb_nodes = len(router_names) + len(ni_names)

    links = []
    connections_yaml = []

    def add_router_link(a, b, latency):
        links.append((a, b, latency))
        connections_yaml.append({"src": a, "dst": b})

    for z in range(dim_z):
        for y in range(dim_y):
            for x in range(dim_x - 1):
                add_router_link(r_name(x, y, z), r_name(x + 1, y, z), arch.link_latency)
            if is_torus and dim_x > 1:
                add_router_link(r_name(dim_x - 1, y, z), r_name(0, y, z), arch.link_latency)
    for z in range(dim_z):
        for x in range(dim_x):
            for y in range(dim_y - 1):
                add_router_link(r_name(x, y, z), r_name(x, y + 1, z), arch.link_latency)
            if is_torus and dim_y > 1:
                add_router_link(r_name(x, dim_y - 1, z), r_name(x, 0, z), arch.link_latency)
    for y in range(dim_y):
        for x in range(dim_x):
            for z in range(dim_z - 1):
                add_router_link(r_name(x, y, z), r_name(x, y, z + 1), arch.link_latency)
            if is_torus and dim_z > 1:
                add_router_link(r_name(x, y, dim_z - 1), r_name(x, y, 0), arch.link_latency)
    for z in range(dim_z):
        for y in range(dim_y):
            for x in range(dim_x):
                links.append((ni_graph_name(ep_name(x, y, z)), r_name(x, y, z), arch.link_latency))
                connections_yaml.append({"src": ep_name(x, y, z), "dst": r_name(x, y, z)})

    floogen_doc = {
        "name": topology_name,
        "description": f"Generated FlooGen topology for SoftHier {topology_name}",
        "network_type": "narrow-wide",
        "routing": {"route_algo": "ID", "use_id_table": True},
        "protocols": _PROTOCOLS,
        "endpoints": endpoints_yaml,
        "routers": routers_yaml,
        "connections": connections_yaml,
    }

    if is_torus:
        # No deadlock-free XYZ-with-wraparound algorithm exists anywhere
        # in this codebase to port (arc_model is 2D-only) -- generic
        # shortest-path BFS matches 3d_torus's actual current live
        # behavior exactly, so it stays the fallback rather than a new,
        # unverified 3D routing design.
        routing_tables = _compute_shortest_path_routing_tables(
            router_names, ni_names, links, nb_nodes, name_to_id, id_to_name)
    else:
        routing_tables = _compute_xyz_routing_tables(
            router_names, ni_names, links, nb_nodes, name_to_id, id_to_name, dim_x, dim_y)

    _write_and_validate(out_dir, topology_name, floogen_doc, routing_tables, links)


def _compute_xyz_routing_tables(router_names, ni_names, links, nb_nodes, name_to_id, id_to_name, dim_x, dim_y):
    """
    X-then-Y-then-Z dimension-order routing for a plain (non-torus) 3D mesh. 
    """
    routers = sorted(name_to_id[n] for n in router_names)
    routers_set = set(routers)
    nis = set(name_to_id[n] for n in ni_names)

    routing_tables = {r: {d: -1 for d in range(nb_nodes)} for r in routers}

    ni_to_router = {}
    for src_name, dst_name, _latency in links:
        a, b = name_to_id[src_name], name_to_id[dst_name]
        if a in nis and b in routers_set:
            ni_to_router[a] = b
        elif b in nis and a in routers_set:
            ni_to_router[b] = a

    for src in routers:
        sx, sy, sz = src % dim_x, (src // dim_x) % dim_y, src // (dim_x * dim_y)
        for dst in range(nb_nodes):
            target_router = dst if dst in routers_set else ni_to_router.get(dst, -1)
            if target_router == -1:
                continue
            if src == target_router:
                routing_tables[src][dst] = dst
                continue
            dx = target_router % dim_x
            dy = (target_router // dim_x) % dim_y
            dz = target_router // (dim_x * dim_y)
            if sx != dx:
                next_hop = src + 1 if dx > sx else src - 1
            elif sy != dy:
                next_hop = src + dim_x if dy > sy else src - dim_x
            else:
                next_hop = src + dim_x * dim_y if dz > sz else src - dim_x * dim_y
            routing_tables[src][dst] = next_hop

    return _finalize_routing_tables(routing_tables, id_to_name, "XYZ")


def _hex_axial_coords(num_cluster):
    """
    Spiral-walks a hex grid outward ring by ring in axial (q, r)
    coordinates, stopping once num_cluster coordinates are collected.
    Shared by hexamesh and folded_hexatorus.
    """
    ring_walk_dirs = [(-1, 1), (-1, 0), (0, -1), (1, -1), (1, 0), (0, 1)]
    coords = [(0, 0)]
    ring = 1
    while len(coords) < num_cluster:
        q, r = ring, 0
        for dq, dr in ring_walk_dirs:
            for _ in range(ring):
                if len(coords) < num_cluster:
                    coords.append((q, r))
                q += dq
                r += dr
        ring += 1
    return coords


def generate_hex(arch, out_dir, topology_name):
    """
    HexaMesh or FoldedHexaTorus: one router+NI per cluster, laid out
    in a hex spiral and connected along the 3 hex axes (East/West, 
    SouthEast/NorthWest, SouthWest/NortheEast).

        for cluster_id, (q, r) in enumerate(coords):
            noc.add_router(cluster_id, num_queues=7)
            noc.add_network_interface(num_cluster + cluster_id)
        # Axis 1 (East/West), Axis 2 (SouthEast/NorthWest),
        # Axis 3 (SouthWest/NortheEast): each direct-neighbor link,
        # or (folded_hexatorus only) the wraparound link found via
        # get_torus_neighbor's periodic lattice vectors
        # NI <-> router links
        noc.generate_routing_tables_shortest_path()

    Router/NI names are flat (router_{cluster_id}, cluster_{cluster_id}),
    matching the ring family's convention, since hex coordinates don't
    map onto a rectangular grid the way mesh's router_{x}_{y} does.
    """
    n = arch.num_cluster
    is_torus = bool(getattr(arch, 'is_torus', False))
    coords = _hex_axial_coords(n)
    coord_to_cluster_id = {coord: i for i, coord in enumerate(coords)}

    router_names = [f"router_{i}" for i in range(n)]
    ni_endpoint_names = [f"cluster_{i}" for i in range(n)]
    ni_names = [ni_graph_name(name) for name in ni_endpoint_names]

    # No "degree" set (see generate_hier_ring): interior vs. edge-of-spiral
    # routers have different neighbor counts, so let floogen auto-derive it.
    routers_yaml = [{"name": name} for name in router_names]
    endpoints_yaml = [_ni_endpoint(name) for name in ni_endpoint_names]

    name_to_id = {name: i for i, name in enumerate(router_names)}
    name_to_id.update({name: n + i for i, name in enumerate(ni_names)})
    id_to_name = {v: k for k, v in name_to_id.items()}
    nb_nodes = 2 * n

    links = []
    connections_yaml = []

    def add_router_link(a, b):
        links.append((a, b, arch.link_latency))
        connections_yaml.append({"src": a, "dst": b})

    if is_torus:
        R = arch.num_rings
        # Off-spiral (q, r) + one of these always lands back on a real
        # coordinate: the finite spiral tiles an infinite hex lattice,
        # and wrapping means jumping by one whole tile.
        c_vectors = [
            (R + 1, R), (-R, 2 * R + 1), (-2 * R - 1, R + 1),
            (-R - 1, -R), (R, -2 * R - 1), (2 * R + 1, -R - 1),
        ]

        def neighbor_cluster_id(q, r, dq, dr):
            nq, nr = q + dq, r + dr
            if (nq, nr) in coord_to_cluster_id:
                return coord_to_cluster_id[(nq, nr)]
            for cq, cr in c_vectors:
                wq, wr = nq - cq, nr - cr
                if (wq, wr) in coord_to_cluster_id:
                    return coord_to_cluster_id[(wq, wr)]
            return -1

        # Folded lattice spacing is double the plain spiral's.
        axis_deltas = [(2, 0), (0, 2), (-2, 2)]
    else:
        def neighbor_cluster_id(q, r, dq, dr):
            return coord_to_cluster_id.get((q + dq, r + dr), -1)

        axis_deltas = [(1, 0), (0, 1), (-1, 1)]

    # A hex grid has 3 axes, not 2: East/West, SouthEast/NorthWest,
    # SouthWest/NorthEast. Walk order matches the original per-topology
    # code's declaration order, which the id assignment below depends on.
    axis_sort_keys = [lambda c: c[0], lambda c: c[1], lambda c: -c[0] + c[1]]
    for (dq, dr), sort_key in zip(axis_deltas, axis_sort_keys):
        for q, r in sorted(coords, key=sort_key, reverse=True):
            neighbor_id = neighbor_cluster_id(q, r, dq, dr)
            if neighbor_id != -1:
                add_router_link(router_names[coord_to_cluster_id[(q, r)]],
                                 router_names[neighbor_id])

    for i in range(n):
        links.append((ni_names[i], router_names[i], arch.link_latency))
        connections_yaml.append({"src": ni_endpoint_names[i], "dst": router_names[i]})

    floogen_doc = {
        "name": topology_name,
        "description": f"Generated FlooGen topology for SoftHier {topology_name}",
        "network_type": "narrow-wide",
        "routing": {"route_algo": "ID", "use_id_table": True},
        "protocols": _PROTOCOLS,
        "endpoints": endpoints_yaml,
        "routers": routers_yaml,
        "connections": connections_yaml,
    }

    if is_torus:
        routing_tables = _compute_spanning_tree_routing_tables(
            router_names, ni_names, links, nb_nodes, name_to_id, id_to_name)
    else:
        routing_tables = _compute_hexamesh_axial_routing_tables(
            router_names, ni_names, links, nb_nodes, name_to_id, id_to_name,
            {name_to_id[router_names[i]]: coords[i] for i in range(n)})

    _write_and_validate(out_dir, topology_name, floogen_doc, routing_tables, links)


def _compute_hexamesh_axial_routing_tables(router_names, ni_names, links, nb_nodes, name_to_id, id_to_name, id_to_coord):
    """
    Axial dimension-order routing for a (non-torus) HexaMesh. Steps
    toward the destination one axial coordinate at a time; if the exact
    next step isn't a real router (possible at the edge of the hex
    spiral), falls back to whichever real neighbor has the smallest hex
    (cube) distance to the destination.
    """
    routers = sorted(name_to_id[n] for n in router_names)
    routers_set = set(routers)
    nis = set(name_to_id[n] for n in ni_names)
    coord_to_id = {coord: r_id for r_id, coord in id_to_coord.items()}

    routing_tables = {r: {d: -1 for d in range(nb_nodes)} for r in routers}

    ni_to_router = {}
    for src_name, dst_name, _latency in links:
        a, b = name_to_id[src_name], name_to_id[dst_name]
        if a in nis and b in routers_set:
            ni_to_router[a] = b
        elif b in nis and a in routers_set:
            ni_to_router[b] = a

    for src in routers:
        src_q, src_r = id_to_coord[src]
        for dst in range(nb_nodes):
            target_router = dst if dst in routers_set else ni_to_router.get(dst, -1)
            if target_router == -1:
                continue
            if src == target_router:
                routing_tables[src][dst] = dst
                continue

            dst_q, dst_r = id_to_coord[target_router]
            if src_q != dst_q:
                next_q = src_q + 1 if dst_q > src_q else src_q - 1
                next_r = src_r
            else:
                next_q = src_q
                next_r = src_r + 1 if dst_r > src_r else src_r - 1

            if (next_q, next_r) in coord_to_id:
                next_hop = coord_to_id[(next_q, next_r)]
            else:
                best_dist = float('inf')
                next_hop = src
                for dq, dr in [(-1, 1), (1, -1), (0, 1), (0, -1), (1, 0), (-1, 0)]:
                    test_q, test_r = src_q + dq, src_r + dr
                    if (test_q, test_r) in coord_to_id:
                        # Axial-to-cube hex distance formula.
                        dist = (abs(test_q - dst_q) + abs(test_q + test_r - dst_q - dst_r) + abs(test_r - dst_r)) // 2
                        if dist < best_dist:
                            best_dist = dist
                            next_hop = coord_to_id[(test_q, test_r)]

            routing_tables[src][dst] = next_hop

    return _finalize_routing_tables(routing_tables, id_to_name, "HexaMesh axial")


def _compute_spanning_tree_routing_tables(router_names, ni_names, links, nb_nodes, name_to_id, id_to_name):
    """
    Multiple-spanning-tree deadlock-free routing.
    Builds num_trees up/down spanning trees rooted at evenly-spaced
    routers; each router is assigned to whichever tree roots closest to
    it, and routes to a given destination via that destination's tree
    using up/down (never-down-then-up) paths, the same deadlock-freedom
    approach as generate_routing_tables_deadlock_free but spread across
    several trees to shorten the average path.
    """
    routers = sorted(name_to_id[n] for n in router_names)
    routers_set = set(routers)
    nis = set(name_to_id[n] for n in ni_names)
    num_trees = 4

    adj = {r: [] for r in routers}
    ni_to_router = {}
    for src_name, dst_name, _latency in links:
        a, b = name_to_id[src_name], name_to_id[dst_name]
        if a in nis and b in routers_set:
            ni_to_router[a] = b
        elif b in nis and a in routers_set:
            ni_to_router[b] = a
        elif a in routers_set and b in routers_set:
            adj[a].append(b)
            adj[b].append(a)
    for r in routers:
        adj[r] = sorted(set(adj[r]))

    # num_trees roots spread evenly around the router list; levels[r] is
    # r's BFS depth from that root (its tree).
    step = max(1, len(routers) // num_trees)
    roots = [routers[(i * step) % len(routers)] for i in range(num_trees)]

    tree_levels = []
    for root in roots:
        levels = {r: -1 for r in routers}
        levels[root] = 0
        queue = [root]
        while queue:
            curr = queue.pop(0)
            for neighbor in adj[curr]:
                if levels[neighbor] == -1:
                    levels[neighbor] = levels[curr] + 1
                    queue.append(neighbor)
        tree_levels.append(levels)

    # Every destination is served by whichever tree it's shallowest in
    # (closest to that tree's root), to keep paths short.
    router_to_tree_idx = {}
    for r in routers:
        closest_tree_idx = 0
        min_dist = float('inf')
        for t_idx, levels in enumerate(tree_levels):
            if levels[r] < min_dist:
                min_dist = levels[r]
                closest_tree_idx = t_idx
        router_to_tree_idx[r] = closest_tree_idx

    # Up/down routing (per tree): "up" means toward the root. A path may
    # go up any number of times, then down any number of times, but never
    # up again after its first down step. best_next_hops[t][src] is src's
    # first hop toward every other router, found by BFS over (router,
    # has_gone_down) states so a state that already turned down never
    # re-explores an "up" move.
    best_next_hops = {i: {r: {} for r in routers} for i in range(num_trees)}
    for t_idx, levels in enumerate(tree_levels):
        def is_up(u, v, lvl=levels):
            if lvl[v] < lvl[u]:
                return True
            if lvl[v] == lvl[u] and v < u:
                return True
            return False

        for src in routers:
            best_first_hop = {}
            visited = {(src, False)}
            bfs_queue = [(src, False, -1)]
            while bfs_queue:
                curr, has_gone_down, first_hop = bfs_queue.pop(0)
                if curr != src and curr not in best_first_hop:
                    best_first_hop[curr] = first_hop
                for neighbor in adj[curr]:
                    going_up = is_up(curr, neighbor)
                    going_down = not going_up
                    if has_gone_down and going_up:
                        continue
                    new_has_gone_down = has_gone_down or going_down
                    state = (neighbor, new_has_gone_down)
                    if state not in visited:
                        visited.add(state)
                        fh = neighbor if first_hop == -1 else first_hop
                        bfs_queue.append((neighbor, new_has_gone_down, fh))
            best_next_hops[t_idx][src] = best_first_hop

    routing_tables = {r: {d: -1 for d in range(nb_nodes)} for r in routers}
    for src in routers:
        for dst in range(nb_nodes):
            target_router = dst if dst in routers_set else ni_to_router.get(dst, -1)
            if target_router == -1:
                continue
            if src == target_router:
                routing_tables[src][dst] = dst
                continue
            assigned_tree = router_to_tree_idx[target_router]
            if target_router in best_next_hops[assigned_tree][src]:
                routing_tables[src][dst] = best_next_hops[assigned_tree][src][target_router]
            else:
                routing_tables[src][dst] = src

    return _finalize_routing_tables(routing_tables, id_to_name, "Multi-spanning-tree")


def _write_and_validate(out_dir, topology_name, floogen_doc, routing_tables, links):

    os.makedirs(out_dir, exist_ok=True)
    floogen_path = os.path.join(out_dir, f"{topology_name}.floogen.yml")
    routing_path = os.path.join(out_dir, f"{topology_name}.routing.yml")
    link_latencies_path = os.path.join(out_dir, f"{topology_name}.link_latencies.yml")

    # Write the link latencies out as a separate file for FlooNoc.load_from_floogen() to read
    link_latencies = {}
    for src_name, dst_name, latency in links:
        link_latencies.setdefault(src_name, {})[dst_name] = latency

    with open(floogen_path, "w") as f:
        yaml.safe_dump(floogen_doc, f, sort_keys=False)
    with open(routing_path, "w") as f:
        yaml.safe_dump(routing_tables, f, sort_keys=False)
    with open(link_latencies_path, "w") as f:
        yaml.safe_dump(link_latencies, f, sort_keys=False)

    # Round-trip validate through floogen's own parser before declaring
    # success, so a schema mistake fails loudly at generation time.
    parse_config(Network, Path(floogen_path))

    print(f'Generated "{floogen_path}"')
    print(f'Generated "{routing_path}"')
    print(f'Generated "{link_latencies_path}"')


GENERATORS = {
    "ring": generate_ring,
    "hierarchical_ring": generate_hier_ring,
    "2d_mesh": generate_mesh2d,
    "2d_torus": generate_mesh2d,
    "3d_mesh": generate_mesh3d,
    "3d_torus": generate_mesh3d,
    "hexamesh": generate_hex,
    "folded_hexatorus": generate_hex,
}


def main():
    parser = argparse.ArgumentParser(
        description="Generate floogen.yml/routing.yml for a SoftHier topology.")
    parser.add_argument("topology", help="Topology name (key into softhier_arch_base.TOPOLOGIES)")
    parser.add_argument("out_dir", help="Directory to write <topology>.floogen.yml/.routing.yml into")
    parser.add_argument("--param", action="append", default=[], metavar="KEY=VALUE",
                         help="Override an individual arch attribute (repeatable)")
    args = parser.parse_args()

    if args.topology not in _arch_base.TOPOLOGIES:
        raise SystemExit(
            f"Unknown topology '{args.topology}', expected one of: "
            f"{sorted(_arch_base.TOPOLOGIES.keys())}")

    overrides = {}
    for item in args.param:
        key, _, value = item.partition('=')
        # Numeric overrides come in as strings from the command line;
        # int() covers every field this generator currently reads
        # (num_cluster, link_latency, ...).
        overrides[key] = int(value)

    arch = _arch_base.TOPOLOGIES[args.topology](**overrides)

    generator = GENERATORS.get(args.topology)
    if generator is None:
        raise SystemExit(
            f"No floogen generator implemented yet for topology "
            f"'{args.topology}' (have: {sorted(GENERATORS.keys())})")

    generator(arch, args.out_dir, args.topology)


if __name__ == "__main__":
    main()
