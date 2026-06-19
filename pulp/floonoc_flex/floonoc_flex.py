#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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

from enum import IntEnum
import gvsoc.systree
import yaml
from pathlib import Path

from floogen.config_parser import parse_config
from floogen.model.network import Network


class FlooNocFlex(gvsoc.systree.Component):
    """FlooNoc instance for a flexible topology

    This instantiates a Flexible Floonoc Topology

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    width: int
        The width in bytes of the interconnect. This gives the number of bytes/cycles each node can
        route. 
    nb_nodes: int
        Number of nodes in the network.
    router_degrees: int
        Degree of ALL routers in the network. For different degrees, leave empty.
    ni_outstanding_reqs: int
        Number of outstanding requests each network interface can inject to the routers. This should
        be increased when the size of the noc increases.
    router_input_queue_size: int
        Size of the routers input queues. This gives the number of requests which can be buffered
        before the source output queue is stalled.
    """
    def __init__(self, parent: gvsoc.systree.Component, name, narrow_width: int, wide_width:int,
            router_degrees: int=0, nb_nodes: int=0, ni_outstanding_reqs: int=8, router_input_queue_size: int=2,
            network_path: str | None = None, routing_path: str | None = None):
        super().__init__(parent, name)

        self.add_sources([
            'pulp/floonoc_flex/floonoc_flex.cpp',
            'pulp/floonoc_flex/floonoc_router_flex.cpp',
            'pulp/floonoc_flex/floonoc_network_interface_flex.cpp',
        ])

        self.add_property('mappings', {})
        self.add_property('routers', [])
        self.add_property('network_interfaces', [])
        self.add_property('ni_outstanding_reqs', ni_outstanding_reqs)
        self.add_property('narrow_width', narrow_width)
        self.add_property('wide_width', wide_width)
        self.add_property('router_input_queue_size', router_input_queue_size)
        
        self.add_property('nb_nodes', nb_nodes)
        self.add_property('links', [])
        self.add_property('router_degrees', router_degrees)       
        self.id_map = {}
        if network_path is not None:
            self.id_map = self.load_from_floogen(network_path, routing_path)

    def __add_mapping(self, name: str, base: int, size: int, node_id: int, remove_offset:int =0):
        self.get_property('mappings')[name] =  {'base': base, 'size': size, 'node_id': node_id, 'remove_offset':remove_offset}

    def add_router(self, node_id: int, num_queues: int):
        """Instantiate a router in the grid.

        Parameters
        ----------
        node_id: int
            ID of the router node
        """
        self.get_property('routers').append([node_id, num_queues])

    def add_network_interface(self, node_id: int):
        """Instantiate a network interface in the grid.

        A network interface should be instantiated at every node where a burst can be injected,
        typically next to each cluster.

        Parameters
        ----------
        node_id: int
            ID of the network interface node
        """
        self.get_property('network_interfaces').append([node_id])

    def add_link(self, src_node_id: int, dest_node_id: int, latency: int = 1):
        """Add a link between two nodes

        Parameters
        ----------
        src_node_id: int
            ID of the source node
        dest_node_id: int
            ID of the destination node
        latency: int
            Latency of the link in cycles
        """
        self.get_property('links').append([src_node_id, dest_node_id, latency])

    def o_NARROW_MAP(self, itf: gvsoc.systree.SlaveItf, base: int, size: int,
        node_id: int, name: str=None, rm_base: bool=False, remove_offset:int =0):
        """Binds the output of a node to a target, associated to a memory-mapped region.

        Parameters
        ----------
        itf: gvsoc.systree.SlaveItf
            Slave interface where requests matching the memory-mapped region will be sent.
        base: int
            Base address of the memory-mapped region.
        size: int
            Size of the memory-mapped region.
        node_id: int
            ID of the target node
        name: str
            name of the mapping. Should be different for each mapping. Taken from itf component if
            it is None
        rm_base: bool
            if True, the base address is substracted to the address of any request going through
        remove_offset: int
            Offset to remove from the address before applying the mapping
        """
        if name is None:
            name = itf.component.name
        if rm_base and remove_offset == 0:
            remove_offset =base
        self.__add_mapping(f"narrow_{name}", base=base, size=size, node_id=node_id, remove_offset=remove_offset)
        self.itf_bind(f"ni_narrow_{node_id}", itf, signature='io')


    def o_WIDE_BIND(self, itf: gvsoc.systree.SlaveItf, node_id: int):
        self.itf_bind(f"ni_wide_{node_id}", itf, signature='io')

    def o_NARROW_BIND(self, itf: gvsoc.systree.SlaveItf, node_id: int):
        self.itf_bind(f"ni_narrow_{node_id}", itf, signature='io')


    def i_NARROW_INPUT(self, node_id: int) -> gvsoc.systree.SlaveItf:
        """Returns the input port of a node.

        Requests can be injected to the noc using this interface. The noc will then
        forward it to the right target.

        Parameters
        ----------
        x: int
            The x position of the node in the grid
        y: int
            The y position of the node in the grid

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, f'narrow_input_{node_id}', signature='io')

    def i_WIDE_INPUT(self, node_id: int) -> gvsoc.systree.SlaveItf:
        """Returns the input port of a node.

        Requests can be injected to the noc using this interface. The noc will then
        forward it to the right target.

        Parameters
        ----------
        x: int
            The x position of the node in the grid
        y: int
            The y position of the node in the grid

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, f'wide_input_{node_id}', signature='io')

    def generate_routing_tables_shortest_path(self):
        """
        Generates routing tables based on the shortest path between 
        all nodes using BFS.
        """
        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')

        routers = [r[0] for r in self.get_property('routers')]
        nis = [n[0] for n in self.get_property('network_interfaces')]

        routing_tables = {str(r): {str(dst): -1 for dst in range(nb_nodes)} for r in routers}

        adj = {r: [] for r in routers}
        ni_to_router = {}
        
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a
            elif node_a in routers and node_b in routers:
                adj[node_a].append(node_b)
                adj[node_b].append(node_a)

        for r in routers:
            adj[r] = list(set(adj[r]))

        for src in routers:
            best_first_hop = {}
            visited = set([src])
            
            queue = []
            
            for neighbor in adj[src]:
                queue.append((neighbor, neighbor))
                visited.add(neighbor)
                best_first_hop[neighbor] = neighbor

            while queue:
                curr, first_hop = queue.pop(0)
                
                for neighbor in adj[curr]:
                    if neighbor not in visited:
                        visited.add(neighbor)
                        best_first_hop[neighbor] = first_hop
                        queue.append((neighbor, first_hop))

            for dst in range(nb_nodes):
                target_router = dst if dst in routers else ni_to_router.get(dst, -1)
                
                if target_router == -1:
                    continue
                
                if src == target_router:
                    routing_tables[str(src)][str(dst)] = dst
                elif target_router in best_first_hop:
                    routing_tables[str(src)][str(dst)] = best_first_hop[target_router]
                else:
                    routing_tables[str(src)][str(dst)] = src

        self.add_property('routing_tables', routing_tables)

    def generate_routing_tables_mesh_2d(self, dim_x: int, dim_y: int):
        """
        Generates routing tables imitating FlooNoC behaviour.
        """
        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')

        routers = [r[0] for r in self.get_property('routers')]
        nis = [n[0] for n in self.get_property('network_interfaces')]

        routing_tables = {str(r): {str(dst): -1 for dst in range(nb_nodes)} for r in routers}

        ni_to_router = {}
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a

        for src in routers:
            for dst in range(nb_nodes):
                target_router = dst if dst in routers else ni_to_router.get(dst, -1)

                if target_router == -1:
                    continue
                if src == target_router:
                    routing_tables[str(src)][str(dst)] = dst
                    continue

                src_x, src_y = src % dim_x, src // dim_x
                dst_x, dst_y = target_router % dim_x, target_router // dim_x

                if src_x != dst_x:
                    next_hop = src + 1 if dst_x > src_x else src - 1
                else:
                    next_hop = src + dim_x if dst_y > src_y else src - dim_x

                routing_tables[str(src)][str(dst)] = next_hop

        self.add_property('routing_tables', routing_tables)

    def generate_routing_tables_mesh_3d(self, dim_x: int, dim_y: int, dim_z: int):
        """
        Generates routing tables for the routers based on grid dimensions.
        """
        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')

        routers = [r[0] for r in self.get_property('routers')]
        nis = [n[0] for n in self.get_property('network_interfaces')]

        routing_tables = {str(r): {str(dst): -1 for dst in range(nb_nodes)} for r in routers}

        ni_to_router = {}
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a

        for src in routers:
            for dst in range(nb_nodes):
                target_router = dst if dst in routers else ni_to_router.get(dst, -1)

                if target_router == -1:
                    continue
                if src == target_router:
                    routing_tables[str(src)][str(dst)] = dst
                    continue

                src_x, src_y, src_z = src % dim_x, (src // dim_x) % dim_y, src // (dim_x * dim_y)
                dst_x, dst_y, dst_z = target_router % dim_x, (target_router // dim_x) % dim_y, target_router // (dim_x * dim_y)

                if src_x != dst_x:
                    next_hop = src + 1 if dst_x > src_x else src - 1
                elif src_y != dst_y:
                    next_hop = src + dim_x if dst_y > src_y else src - dim_x
                else:
                    next_hop = src + dim_x * dim_y if dst_z > src_z else src - dim_x * dim_y

                routing_tables[str(src)][str(dst)] = next_hop

        self.add_property('routing_tables', routing_tables)

    def generate_routing_tables_ring(self):
        """
        Generates routing tables for a Ring topology.
        """
        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')

        routers = sorted([r[0] for r in self.get_property('routers')])
        nis = [n[0] for n in self.get_property('network_interfaces')]
        num_routers = len(routers)

        routing_tables = {str(r): {str(dst): -1 for dst in range(nb_nodes)} for r in routers}

        ni_to_router = {}
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a

        for src in routers:
            src_idx = routers.index(src)
            for dst in range(nb_nodes):
                
                target_router = dst if dst in routers else ni_to_router.get(dst, -1)

                if target_router == -1:
                    continue
                
                if src == target_router:
                    routing_tables[str(src)][str(dst)] = dst
                    continue

                target_idx = routers.index(target_router)

                if target_idx > src_idx:
                    next_hop_idx = src_idx + 1
                else:
                    next_hop_idx = src_idx - 1

                next_hop = routers[next_hop_idx]
                routing_tables[str(src)][str(dst)] = next_hop

        self.add_property('routing_tables', routing_tables)

    def generate_routing_tables_hier_ring(self, dim_g: int, dim_l: int):
        """
        Generates routing tables for a Hierarchical Ring topology.
        """
        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')

        routers = sorted([r[0] for r in self.get_property('routers')])
        nis = [n[0] for n in self.get_property('network_interfaces')]
        
        N = dim_g * dim_l

        routing_tables = {str(r): {str(dst): -1 for dst in range(nb_nodes)} for r in routers}

        ni_to_router = {}
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a

        for src in routers:
            for dst in range(nb_nodes):
                
                target_router = dst if dst in routers else ni_to_router.get(dst, -1)

                if target_router == -1:
                    continue
                
                if src == target_router:
                    routing_tables[str(src)][str(dst)] = dst
                    continue

                if target_router >= N: 
                    target_g = target_router - N 
                else:
                    target_g = target_router // dim_l

                if src >= N:
                    src_g = src - N
                    
                    if src_g == target_g:
                        next_hop = target_g * dim_l
                    else:
                        if target_g > src_g:
                            next_hop = N + src_g + 1
                        else:
                            next_hop = N + src_g - 1

                else:
                    src_g = src // dim_l
                    bridge_id = src_g * dim_l
                    
                    if src_g == target_g:
                        if target_router > src:
                            next_hop = src + 1
                        else:
                            next_hop = src - 1
                    else:
                        if src == bridge_id:
                            next_hop = N + src_g
                        else:
                            next_hop = src - 1

                routing_tables[str(src)][str(dst)] = next_hop

        self.add_property('routing_tables', routing_tables)

    def generate_routing_tables_deadlock_free(self):
        """
        Generates deadlock-free routing tables for any topology using the Up/Down Spanning Tree Routing Algorithm.
        It is however not minimal in terms of hops and therefore sacrifices BW/Latency.
        To be used in topologies with deadlocks.
        """
        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')

        routers = [r[0] for r in self.get_property('routers')]
        nis = [n[0] for n in self.get_property('network_interfaces')]

        routing_tables = {str(r): {str(dst): -1 for dst in range(nb_nodes)} for r in routers}

        adj = {r: [] for r in routers}
        ni_to_router = {}
        
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a
            elif node_a in routers and node_b in routers:
                adj[node_a].append(node_b)
                adj[node_b].append(node_a)

        for r in routers:
            adj[r] = list(set(adj[r]))

        root = routers[len(routers) // 2] 
        levels = {r: -1 for r in routers}
        levels[root] = 0
        
        queue = [root]
        while queue:
            curr = queue.pop(0)
            for neighbor in adj[curr]:
                if levels[neighbor] == -1:
                    levels[neighbor] = levels[curr] + 1
                    queue.append(neighbor)

        def is_up(u, v):
            if levels[v] < levels[u]:
                return True
            if levels[v] == levels[u] and v < u:
                return True
            return False

        for src in routers:
            best_first_hop = {}
            visited = set() 
            visited.add((src, False))
            
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

            for dst in range(nb_nodes):
                target_router = dst if dst in routers else ni_to_router.get(dst, -1)
                
                if target_router == -1:
                    continue
                if src == target_router:
                    routing_tables[str(src)][str(dst)] = dst
                elif target_router in best_first_hop:
                    routing_tables[str(src)][str(dst)] = best_first_hop[target_router]
                else:
                    routing_tables[str(src)][str(dst)] = src

        self.add_property('routing_tables', routing_tables)

    def generate_routing_tables_hexamesh(self):
        """
        Generates routing tables for a HexaMesh topology using Axial Dimension Order Routing.
        """
        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')

        routers = sorted([r[0] for r in self.get_property('routers')])
        nis = [n[0] for n in self.get_property('network_interfaces')]
        num_routers = len(routers)

        routing_tables = {str(r): {str(dst): -1 for dst in range(nb_nodes)} for r in routers}

        ni_to_router = {}
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a

        ring_walk_dirs = [(-1, 1), (-1, 0), (0, -1), (1, -1), (1, 0), (0, 1)]
        coords = [(0, 0)]
        ring = 1
        
        while len(coords) < num_routers:
            q, r = ring, 0 
            for dq, dr in ring_walk_dirs:
                for _ in range(ring):
                    if len(coords) < num_routers:
                        coords.append((q, r))
                    q += dq
                    r += dr
            ring += 1

        id_to_coord = {r_id: coord for r_id, coord in zip(routers, coords)}
        coord_to_id = {coord: r_id for r_id, coord in zip(routers, coords)}

        for src in routers:
            src_q, src_r = id_to_coord[src]

            for dst in range(nb_nodes):
                target_router = dst if dst in routers else ni_to_router.get(dst, -1)
                
                if target_router == -1:
                    continue 
                
                if src == target_router:
                    routing_tables[str(src)][str(dst)] = dst
                    continue

                dst_q, dst_r = id_to_coord[target_router]

                if src_q != dst_q:
                    next_q = src_q + 1 if dst_q > src_q else src_q - 1
                    next_r = src_r
                else:
                    next_q = src_q
                    next_r = src_r + 1 if dst_r > src_r else src_r - 1

                if (next_q, next_r) in coord_to_id:
                    next_hop_router = coord_to_id[(next_q, next_r)]
                else:
                    best_dist = float('inf')
                    best_neighbor = src
                    
                    for dq, dr in [(-1, 1), (1, -1), (0, 1), (0, -1), (1, 0), (-1, 0)]:
                        test_q, test_r = src_q + dq, src_r + dr
                        
                        if (test_q, test_r) in coord_to_id:
                            dist = (abs(test_q - dst_q) + abs(test_q + test_r - dst_q - dst_r) + abs(test_r - dst_r)) // 2
                            if dist < best_dist:
                                best_dist = dist
                                best_neighbor = coord_to_id[(test_q, test_r)]
                                
                    next_hop_router = best_neighbor

                routing_tables[str(src)][str(dst)] = next_hop_router

        self.add_property('routing_tables', routing_tables)
    
    def generate_routing_tables_fht(self):
        """
        Generates routing tables using Multiple Spanning Trees.
        Can be faster than simple Up/Down.
        """
        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')
        num_trees = 4

        routers = sorted([r[0] for r in self.get_property('routers')])
        nis = [n[0] for n in self.get_property('network_interfaces')]

        routing_tables = {str(r): {str(dst): -1 for dst in range(nb_nodes)} for r in routers}

        adj = {r: [] for r in routers}
        ni_to_router = {}
        
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a
            elif node_a in routers and node_b in routers:
                adj[node_a].append(node_b)
                adj[node_b].append(node_a)

        for r in routers:
            adj[r] = sorted(list(set(adj[r])))

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

        router_to_tree_idx = {}
        for r in routers:
            closest_tree_idx = 0
            min_dist = float('inf')
            for t_idx, levels in enumerate(tree_levels):
                if levels[r] < min_dist:
                    min_dist = levels[r]
                    closest_tree_idx = t_idx
            router_to_tree_idx[r] = closest_tree_idx

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
                visited = set() 
                visited.add((src, False))
                
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

        for src in routers:
            for dst in range(nb_nodes):
                target_router = dst if dst in routers else ni_to_router.get(dst, -1)
                
                if target_router == -1:
                    continue
                if src == target_router:
                    routing_tables[str(src)][str(dst)] = dst
                    continue
                
                assigned_tree = router_to_tree_idx[target_router]
                
                if target_router in best_next_hops[assigned_tree][src]:
                    routing_tables[str(src)][str(dst)] = best_next_hops[assigned_tree][src][target_router]
                else:
                    routing_tables[str(src)][str(dst)] = src

        self.add_property('routing_tables', routing_tables)

    def generate_routing_tables_arc_model(self, dim_x: int, dim_y: int):
        """
        Implementation of Algorithm 3 from 'Developing Deadlock-Free Routing Algorithms in Torus NoC'
        Uses (EWs + WEs + NSe + SN FirstHop) arcs combined with XY routing.
        """
        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')
        routers = [r[0] for r in self.get_property('routers')]
        nis = [n[0] for n in self.get_property('network_interfaces')]

        routing_tables = {str(r): {str(dst): -1 for dst in range(nb_nodes)} for r in routers}

        ni_to_router = {}
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a

        min_x, max_x = 1, dim_x - 2
        min_y, max_y = 1, dim_y - 2
        w = max_x - min_x + 1
        h = max_y - min_y + 1

        for src in routers:
            sx = src % dim_x
            sy = src // dim_x

            for dst in range(nb_nodes):
                target_router = dst if dst in routers else ni_to_router.get(dst, -1)
                
                if target_router == -1:
                    continue
                if src == target_router:
                    routing_tables[str(src)][str(dst)] = dst
                    continue

                dx = target_router % dim_x
                dy = target_router // dim_x
                
                delta_x = abs(dx - sx)
                delta_y = abs(dy - sy)
                
                if (dy > sy) and (sx > dx) and (delta_x > w // 2):
                    if sx == max_x:
                        next_sx = min_x
                    else:
                        next_sx = sx + 1
                    next_hop = sy * dim_x + next_sx
                    
                elif (dy > sy) and (dx > sx) and (delta_x > w // 2):
                    if sx == min_x:
                        next_sx = max_x
                    else:
                        next_sx = sx - 1
                    next_hop = sy * dim_x + next_sx

                elif (dx > sx) and (dy > sy) and (delta_y > h // 2):
                    if sy == min_y:
                        next_sy = max_y
                    else:
                        next_sy = sy - 1
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

                routing_tables[str(src)][str(dst)] = next_hop

        self.add_property('routing_tables', routing_tables)


    def load_from_floogen(self, network_path: str, routing_path: str = None):
            """
            Parses a FlooGen YAML file and populates the GVSoC NoC topology.
            """

            # Parse the YAML into FlooGen's Network object
            floo_net = parse_config(Network, Path(network_path))
            floo_net.create_network()
            floo_net.compile_network()

            # Compatibility layer: Maps FlooGen string names to our FloonocFlex IDs
            node_to_id = {}
            current_id = 0

            # Extract Routers
            for rt_name, rt_node in floo_net.graph.get_rt_nodes(with_name=True):
                node_to_id[rt_name] = current_id
                num_queues = getattr(rt_node, 'degree', 5)
                self.add_router(current_id, num_queues=num_queues)
                current_id += 1

            # Extract Network Interfaces (NIs)
            for ni_name, _ in floo_net.graph.get_ni_nodes(with_name=True):
                node_to_id[ni_name] = current_id
                self.add_network_interface(current_id)
                current_id += 1

            self.add_property('nb_nodes', current_id)
            self.id_map = node_to_id

            added_links = set()
            
            # Extract Links
            for src_name, dst_name in floo_net.graph.get_link_edges(with_obj=False, with_name=True):
                # Ensure both sides of the link exist in our ID map
                if src_name in node_to_id and dst_name in node_to_id:
                    src_id = node_to_id[src_name]
                    dst_id = node_to_id[dst_name]
                    
                    link_pair = tuple(sorted((src_id, dst_id)))
                    
                    # Only add bidirectional link if not already added
                    if link_pair not in added_links:
                        self.add_link(src_id, dst_id, latency=1)
                        added_links.add(link_pair)

            # Generate Routing Tables
            self.generate_routing_tables_shortest_path()
            
            # Return the dictionary, just in case
            return node_to_id