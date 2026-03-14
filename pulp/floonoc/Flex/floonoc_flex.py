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

class FlooNocDirection(IntEnum):
    RIGHT = -4
    LEFT = -3
    UP = -2
    DOWN = -1
    SELF = 1
    DIR_1 = 2
    DIR_2 = 3

class FlooNoc2dMeshNarrowWide(gvsoc.systree.Component): #Rename this and rewrite description to something generic
    """FlooNoc instance for a 2D mesh

    This instantiates a FlooNoc 2D mesh for a grid of clusters.
    It contains:
     - a central part made of one cluster, one router and one network interface at each node
     - a border of targets
    If the grid contains X by Y clusters, the whole grid is X+2 by Y+2 as there are borders in each
    direction for the targets.

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    width: int
        The width in bytes of the interconnect. This gives the number of bytes/cycles each node can
        route.
    dim_x: int
        The X dimension of the grid. This should also include targets on the borders.
    dim_y: int
        The Y dimension of the grid. This should also include targets on the borders.
    ni_outstanding_reqs: int
        Number of outstanding requests each network interface can inject to the routers. This should
        be increased when the size of the noc increases.
    router_input_queue_size: int
        Size of the routers input queues. This gives the number of requests which can be buffered
        before the source output queue is stalled.
    """
    def __init__(self, parent: gvsoc.systree.Component, name, narrow_width: int, wide_width:int,
            ni_outstanding_reqs: int=8, router_input_queue_size: int=2, nb_nodes: int=0, 
            router_degrees: int=5):
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

    def add_router_node(self, node_id: int):
        """Instantiate a router for flexible topology.

        Parameters
        ----------
        node_id: int
            ID of the router node
        """
        self.get_property('routers').append([node_id])

    def add_network_interface_node(self, node_id: int):
        """Instantiate a network interface for flexible topology.

        Parameters
        ----------
        node_id: int
            ID of the network interface node
        """
        self.get_property('network_interfaces').append([node_id])

    def add_route(self, src_node_id: int, dest_node_id: int, next_node_id: int):
        """Add a route for flexible topology."""
        pass

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