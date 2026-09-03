#!/usr/bin/env python3
"""
Generates a small 3D-mesh FlooGen topology for the flexible FlooNoc tutorial.

This is a trimmed, standalone version of the topology generator used for the
real SoftHier chip (pulp/pulp/chips/softhier/topologies/gen_floogen_topology.py),
kept to the single 3D-mesh case and extended with a *separate* Z-axis link
latency, to make the point that floogen's own schema has no per-link timing
field: GVSoC's flexible FlooNoc model (FlooNocFlex) reads per-link latencies
from a side file (<name>.link_latencies.yml) that this script also produces.

It writes three files under --out-dir:
    <name>.floogen.yml           the floogen network description (nodes, links, routing algo)
    <name>.routing.yml           precomputed {src: {dst: next_hop}} routing tables
    <name>.link_latencies.yml    precomputed {src: {dst: latency}} per-link latencies

Usage:
    python3 gen_topology.py --dim-x 2 --dim-y 2 --dim-z 2 \\
        --link-latency 1 --z-link-latency 8 --out-dir generated --name mesh3d
"""

import argparse
import os
from pathlib import Path

import yaml

from floogen.config_parser import parse_config
from floogen.model.network import Network


# Boilerplate AXI4 narrow/wide channel declarations, required by floogen's
# schema but not relevant to this tutorial.
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
    # floogen unconditionally appends "_ni" to get the graph node name.
    return {"name": name, "mgr_port_protocol": ["narrow_in", "wide_in"]}


def ni_graph_name(name):
    return f"{name}_ni"


def r_name(x, y, z):
    return f"router_{x}_{y}_{z}"


def ep_name(x, y, z):
    return f"cluster_{x}_{y}_{z}"


def generate_mesh3d(dim_x, dim_y, dim_z, link_latency, z_link_latency):
    """Builds the floogen doc + per-link latencies for a plain (non-torus)
    3D mesh, with X/Y links carrying `link_latency` and Z links carrying the
    (typically larger, e.g. modeling die-to-die/TSV links) `z_link_latency`.
    """
    router_names = [r_name(x, y, z)
                     for z in range(dim_z) for y in range(dim_y) for x in range(dim_x)]
    ni_endpoint_names = [ep_name(x, y, z)
                          for z in range(dim_z) for y in range(dim_y) for x in range(dim_x)]
    ni_names = [ni_graph_name(name) for name in ni_endpoint_names]

    routers_yaml = [{"name": name} for name in router_names]
    endpoints_yaml = [_ni_endpoint(name) for name in ni_endpoint_names]

    name_to_id = {name: i for i, name in enumerate(router_names)}
    name_to_id.update({name: len(router_names) + i for i, name in enumerate(ni_names)})
    id_to_name = {v: k for k, v in name_to_id.items()}
    nb_nodes = len(router_names) + len(ni_names)

    links = []
    connections_yaml = []

    def add_link(a, b, latency):
        links.append((a, b, latency))
        connections_yaml.append({"src": a, "dst": b})

    # X links
    for z in range(dim_z):
        for y in range(dim_y):
            for x in range(dim_x - 1):
                add_link(r_name(x, y, z), r_name(x + 1, y, z), link_latency)
    # Y links
    for z in range(dim_z):
        for x in range(dim_x):
            for y in range(dim_y - 1):
                add_link(r_name(x, y, z), r_name(x, y + 1, z), link_latency)
    # Z links: the axis this tutorial gives a distinct latency to (e.g. a
    # slower inter-die/TSV hop compared to the in-die X/Y mesh links).
    for y in range(dim_y):
        for x in range(dim_x):
            for z in range(dim_z - 1):
                add_link(r_name(x, y, z), r_name(x, y, z + 1), z_link_latency)
    # NI <-> router links
    for z in range(dim_z):
        for y in range(dim_y):
            for x in range(dim_x):
                add_link(ni_graph_name(ep_name(x, y, z)), r_name(x, y, z), link_latency)

    floogen_doc = {
        "name": "mesh3d",
        "description": "Tutorial 3D mesh with a variable-latency Z axis",
        "network_type": "narrow-wide",
        "routing": {"route_algo": "ID", "use_id_table": True},
        "protocols": _PROTOCOLS,
        "endpoints": endpoints_yaml,
        "routers": routers_yaml,
        "connections": connections_yaml,
    }

    routing_tables = _compute_xyz_routing_tables(
        router_names, ni_names, links, nb_nodes, name_to_id, id_to_name, dim_x, dim_y)

    return floogen_doc, routing_tables, links


def _compute_xyz_routing_tables(router_names, ni_names, links, nb_nodes, name_to_id, id_to_name,
        dim_x, dim_y):
    """X-then-Y-then-Z dimension-order routing for a plain (non-torus) 3D mesh."""
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

    return _finalize_routing_tables(routing_tables, id_to_name)


def _finalize_routing_tables(routing_tables, id_to_name):
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
        raise RuntimeError(f"XYZ routing left {len(missing)} unresolved entries, "
            f"e.g. {missing[:5]}")
    return named_tables


def write_and_validate(out_dir, name, floogen_doc, routing_tables, links):
    os.makedirs(out_dir, exist_ok=True)
    floogen_path = os.path.join(out_dir, f"{name}.floogen.yml")
    routing_path = os.path.join(out_dir, f"{name}.routing.yml")
    link_latencies_path = os.path.join(out_dir, f"{name}.link_latencies.yml")

    link_latencies = {}
    for src_name, dst_name, latency in links:
        link_latencies.setdefault(src_name, {})[dst_name] = latency

    with open(floogen_path, "w") as f:
        yaml.safe_dump(floogen_doc, f, sort_keys=False)
    with open(routing_path, "w") as f:
        yaml.safe_dump(routing_tables, f, sort_keys=False)
    with open(link_latencies_path, "w") as f:
        yaml.safe_dump(link_latencies, f, sort_keys=False)

    # Round-trip validate through floogen's own parser before declaring success.
    parse_config(Network, Path(floogen_path))

    print(f'Generated "{floogen_path}"')
    print(f'Generated "{routing_path}"')
    print(f'Generated "{link_latencies_path}"')

    return floogen_path, routing_path, link_latencies_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dim-x', type=int, default=2)
    parser.add_argument('--dim-y', type=int, default=2)
    parser.add_argument('--dim-z', type=int, default=2)
    parser.add_argument('--link-latency', type=int, default=1,
        help='Latency in cycles of every X and Y link')
    parser.add_argument('--z-link-latency', type=int, default=8,
        help='Latency in cycles of every Z link (kept distinct to show per-link overrides)')
    parser.add_argument('--out-dir', default='generated')
    parser.add_argument('--name', default='mesh3d')
    args = parser.parse_args()

    floogen_doc, routing_tables, links = generate_mesh3d(
        args.dim_x, args.dim_y, args.dim_z, args.link_latency, args.z_link_latency)

    write_and_validate(args.out_dir, args.name, floogen_doc, routing_tables, links)


if __name__ == '__main__':
    main()
