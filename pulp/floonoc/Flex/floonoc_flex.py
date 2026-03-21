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

"""
class FlooNocDirection(IntEnum):
    RIGHT = -4
    LEFT = -3
    UP = -2
    DOWN = -1
    SELF = 1
    DIR_1 = 2
    DIR_2 = 3
""" 
class FlooNocDirection(IntEnum):
    DIR_LOCAL = 1
    DIR_1 = 2
    DIR_2 = 3
    DIR_3 = 4
    DIR_4 = 5
    DIR_5 = 6
    DIR_6 = 7

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
            router_degrees: int=0, nb_nodes: int=0, ni_outstanding_reqs: int=8, router_input_queue_size: int=2):
        super().__init__(parent, name)

        self.add_sources([
            'pulp/floonoc/floonoc_flex.cpp',
            'pulp/floonoc/floonoc_router_flex.cpp',
            'pulp/floonoc/floonoc_network_interface_flex.cpp',
        ])

        self.add_property('mappings', {})
        self.add_property('routers', [])
        self.add_property('network_interfaces', [])
        self.add_property('ni_outstanding_reqs', ni_outstanding_reqs)
        self.add_property('narrow_width', narrow_width)
        self.add_property('wide_width', wide_width)
        self.add_property('router_input_queue_size', router_input_queue_size)
        
        # Support for flexible topologies
        self.add_property('nb_nodes', nb_nodes)
        self.add_property('links', [])
        self.add_property('router_degrees', router_degrees)        

    def __add_mapping(self, name: str, base: int, size: int, node_id: int, remove_offset:int =0):
        self.get_property('mappings')[name] =  {'base': base, 'size': size, 'node_id': node_id, 'remove_offset':remove_offset}

    def add_router(self, node_id: int):
        """Instantiate a router in the grid.

        Parameters
        ----------
        node_id: int
            ID of the router node
        """
        self.get_property('routers').append([node_id])

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

    def o_MAP_DIR(self, base: int, size: int, dir: FlooNocDirection, name: str,
            rm_base: bool=False, remove_offset:int =0):
        if rm_base and remove_offset == 0:
            remove_offset =base
        self.__add_mapping(f"ni_{name}", base=base, size=size, node_id=0, remove_offset=remove_offset) #Placeholder node id rn

    def o_MAP(self, base: int, size: int,
            x: int, y: int,
            rm_base: bool=False, remove_offset:int =0):
        """Binds the output of a node to a target, associated to a memory-mapped region.

        Parameters
        ----------
        base: int
            Base address of the memory-mapped region.
        size: int
            Size of the memory-mapped region.
        x: int
            X position of the target in the grid
        y: int
            Y position of the target in the grid
        name: str
            name of the mapping. Should be different for each mapping. Taken from itf component if
            it is None
        rm_base: bool
            if True, the base address is substracted to the address of any request going through
        remove_offset: int
            Offset to remove from the address before applying the mapping
        """
        if rm_base and remove_offset == 0:
            remove_offset =base
        self.__add_mapping(f"ni_{x}_{y}", base=base, size=size, node_id=0, remove_offset=remove_offset) #Placeholder node id rn

    def o_WIDE_MAP(self, itf: gvsoc.systree.SlaveItf | None, base: int, size: int,
            x: int | FlooNocDirection, y: int | FlooNocDirection, name: str | None=None,
            rm_base: bool=False, remove_offset:int =0):
        """This methods is deprecated
        """
        if name is None:
            name = itf.component.name
        if rm_base and remove_offset == 0:
            remove_offset =base
        self.__add_mapping(f"wide_{name}", base=base, size=size, node_id=0, remove_offset=remove_offset) #Placeholder node id rn
        self.itf_bind(f"ni_wide_{x}_{y}", itf, signature='io')

    def i_NARROW_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
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
        return gvsoc.systree.SlaveItf(self, f'narrow_input_{x}_{y}', signature='io')

    def i_WIDE_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
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
        return gvsoc.systree.SlaveItf(self, f'wide_input_{x}_{y}', signature='io')

def __generate_routing_tables(self, dim_x=1, dim_y=1, dim_z=1):
        """
        Generates routing tables for the routers based on grid dimensions.
        Safely handles sparse Network Interface (NI) IDs.
        """
        # 1 = 2D_MESH (XY Routing)
        # 2 = 3D_MESH (Z-XY Routing)
        # 3 = CUSTOM
        routing_mode = 1 

        nb_nodes = self.get_property('nb_nodes')
        links = self.get_property('links')
        
        # Extract the IDs of our components
        routers = [r[0] for r in self.get_property('routers')]
        nis = [n[0] for n in self.get_property('network_interfaces')]

        # Build the NI-to-Router physical mapping from the links array
        ni_to_router = {}
        for link in links:
            node_a, node_b = link[0], link[1]
            if node_a in nis and node_b in routers:
                ni_to_router[node_a] = node_b
            elif node_b in nis and node_a in routers:
                ni_to_router[node_b] = node_a

        # Initialize routing tables
        routing_tables = {r: {} for r in routers}

        for src in routers:
            for dst in range(nb_nodes):
                
                # Resolve the target router for this destination
                if dst in routers:
                    target_router = dst
                elif dst in nis:
                    target_router = ni_to_router.get(dst, -1)
                else:
                    target_router = -1 # Unmapped or empty node ID

                # Base Cases
                if target_router == -1:
                    routing_tables[src][dst] = -1
                    continue
                if src == target_router:
                    # We are at the router physically attached to the destination
                    # The next hop is the destination itself!
                    routing_tables[src][dst] = dst
                    continue

                # Apply Routing Math (from src router to target_router)
                # Note: We assume Router IDs are sequentially numbered 0 to (W*H - 1)
                if routing_mode == 1: # 2D_MESH
                    src_x, src_y = src % dim_x, src // dim_x
                    dst_x, dst_y = target_router % dim_x, target_router // dim_x

                    if src_x != dst_x:
                        next_hop = src + 1 if dst_x > src_x else src - 1
                    else:
                        next_hop = src + dim_x if dst_y > src_y else src - dim_x
                        
                    routing_tables[src][dst] = next_hop

                elif routing_mode == 2: # 3D_MESH
                    layer_size = dim_x * dim_y
                    src_z, rem_src = src // layer_size, src % layer_size
                    src_y, src_x = rem_src // dim_x, rem_src % dim_x
                    
                    dst_z, rem_dst = target_router // layer_size, target_router % layer_size
                    dst_y, dst_x = rem_dst // dim_x, rem_dst % dim_x

                    if src_z != dst_z:
                        next_hop = src + layer_size if dst_z > src_z else src - layer_size
                    elif src_x != dst_x:
                        next_hop = src + 1 if dst_x > src_x else src - 1
                    else:
                        next_hop = src + dim_x if dst_y > src_y else src - dim_x
                        
                    routing_tables[src][dst] = next_hop

                elif routing_mode == 3: # CUSTOM
                    # Custom routing logic
                    routing_tables[src][dst] = -1
        
        self.add_property('routing_tables', routing_tables)