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
#

"""FlooGen front-end of the FlooNoC v2 graph NoC.

Kept free of simulator imports, like floonoc_v2_routing, so it can be used
and tested outside a platform build. Needs the floogen Python package (the
FlooNoC network generator).
"""

import yaml
from pathlib import Path


def load_floogen(noc, network_path: str, routing_path: str=None,
        link_latencies_path: str=None, default_link_latency: int=1, tiles=None) -> dict:
    """Build a graph FlooNoC from a FlooGen network description.

    noc is a FlooNocV2Graph (or anything with its add_router /
    add_network_interface / add_link / set_port_order methods and a
    ``fabric`` whose routing can be set). Routers get the node IDs 0..R-1 and
    NIs R..R+N-1, in FlooGen graph order, like FlooNoC-Flex, and their ports
    follow the FlooGen port order. The routing follows the FlooGen
    route_algo:

    - "ID": FlooGen's own router tables (the RTL ones), unless routing_path
      gives a next-hop table ({router: {dest: next_hop}}, node names).
    - "XY": dimension-order routing on the router coordinates.
    - "SRC": routing_path is required. Source routing can send packets from
      different sources through the same router on different paths, which
      per-router tables cannot express in general.

    FlooGen has no link timing: link_latencies_path optionally gives
    per-link latencies ({src: {dst: latency}}, node names, either
    direction), other links take default_link_latency. A latency of L
    cycles adds L-1 pipeline stages to the base link.

    tiles optionally groups the nodes into tiles (see FlooNocV2GraphFabric,
    noc must then be the fabric): a callable taking a node name and returning
    the tile of that node, or None. A router linked to exactly one NI goes
    into the tile of that NI, the other routers into the tile of their own
    name.

    Returns the node name -> node ID map.
    """
    from floogen.config_parser import parse_config
    from floogen.model.network import Network

    floo_net = parse_config(Network, Path(network_path))
    floo_net.create_network()
    floo_net.compile_network()
    floo_net.gen_routing_info()
    graph = floo_net.graph

    link_latencies = {}
    if link_latencies_path is not None:
        with open(link_latencies_path, 'r') as file:
            for src_name, dsts in (yaml.safe_load(file) or {}).items():
                for dst_name, latency in dsts.items():
                    link_latencies[(src_name, dst_name)] = latency

    # Tile of each node: a router shares the tile of its only NI
    node_tiles = {}
    if tiles is not None:
        ni_names = [ni_name for ni_name, _ in graph.get_ni_nodes(with_name=True)]
        router_nis = {}
        for src_name, dst_name in graph.get_link_edges(with_obj=False, with_name=True):
            if dst_name in ni_names:
                router_nis.setdefault(src_name, set()).add(dst_name)
        for ni_name in ni_names:
            node_tiles[ni_name] = tiles(ni_name)
        for rt_name, _ in graph.get_rt_nodes(with_name=True):
            nis = router_nis.get(rt_name, set())
            node_tiles[rt_name] = node_tiles[next(iter(nis))] if len(nis) == 1 \
                else tiles(rt_name)

    def tile_args(name):
        return {'tile': node_tiles[name]} if tiles is not None else {}

    id_map = {}
    route_algo = floo_net.routing.route_algo.name
    for rt_name, rt_node in graph.get_rt_nodes(with_name=True):
        id_map[rt_name] = len(id_map)
        coord = None
        if route_algo == 'XY' and rt_node.id is not None:
            coord = (rt_node.id.x, rt_node.id.y)
        noc.add_router(id_map[rt_name], name=rt_name, coord=coord, **tile_args(rt_name))
    for ni_name, _ in graph.get_ni_nodes(with_name=True):
        id_map[ni_name] = len(id_map)
        noc.add_network_interface(id_map[ni_name], name=ni_name, **tile_args(ni_name))

    def link_stages(src_name, dst_name):
        latency = link_latencies.get((src_name, dst_name),
            link_latencies.get((dst_name, src_name), default_link_latency))
        if latency < 1:
            raise RuntimeError(f'Link {src_name} -> {dst_name}: latency must be at least 1')
        return latency - 1

    linked = set()
    for src_name, dst_name in graph.get_link_edges(with_obj=False, with_name=True):
        if (dst_name, src_name) in linked:
            continue
        linked.add((src_name, dst_name))
        noc.add_link(id_map[src_name], id_map[dst_name],
            stages=link_stages(src_name, dst_name), stages_back=link_stages(dst_name, src_name))

    # Router ports in the FlooGen (RTL) order, which is also the round-robin
    # arbitration order.
    for rt_name, rt_node in graph.get_rt_nodes(with_name=True):
        noc.set_port_order(id_map[rt_name],
            [id_map[link.dest] for link in rt_node.outgoing if link is not None])

    fabric = noc.fabric
    if routing_path is not None:
        with open(routing_path, 'r') as file:
            fabric.routing = yaml.safe_load(file)
    elif route_algo == 'XY':
        fabric.routing = 'dimension_order'
        fabric.routing_args = {'order': 'xy'}
    elif route_algo == 'ID':
        fabric.routing = _floogen_id_tables(floo_net, id_map)
    else:
        raise RuntimeError(f'FlooGen route_algo {route_algo} needs an explicit routing table '
            '(routing_path)')

    return id_map


def _floogen_id_tables(floo_net, id_map: dict) -> dict:
    """Next-hop tables of the FlooGen ID-routed routers.

    A FlooGen table rule maps a range of FlooGen NI IDs to an output link of
    the router: each NI ID is looked up like the RTL addr_decode does.
    """
    graph = floo_net.graph
    ni_ids = {ni_name: ni.id.id for ni_name, ni in graph.get_ni_nodes(with_name=True)}
    tables = {}
    for rt_name, rt_node in graph.get_rt_nodes(with_name=True):
        routes = {}
        for ni_name, floo_id in ni_ids.items():
            for rule in rt_node.table.rules:
                if rule.addr_range.start <= floo_id < rule.addr_range.end:
                    routes[ni_name] = rt_node.outgoing[rule.dest.id].dest
                    break
        tables[rt_name] = routes
    return tables
