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

import gvsoc.systree


class FlooNoc2dMeshNarrowWide(gvsoc.systree.Component):
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
            dim_x: int, dim_y:int, ni_outstanding_reqs: int=8, router_input_queue_size: int=2):
        super().__init__(parent, name)

        self.add_sources([
            'pulp/floonoc/floonoc.cpp',
            'pulp/floonoc/floonoc_router.cpp',
            'pulp/floonoc/floonoc_network_interface.cpp',
        ])

        self.add_property('mappings', {})
        self.add_property('routers', [])
        self.add_property('network_interfaces', [])
        self.add_property('ni_outstanding_reqs', ni_outstanding_reqs)
        self.add_property('narrow_width', narrow_width)
        self.add_property('wide_width', wide_width)
        self.add_property('dim_x', dim_x)
        self.add_property('dim_y', dim_y)
        self.add_property('router_input_queue_size', router_input_queue_size)

    def __add_mapping(self, name: str, base: int, size: int, x: int, y: int, remove_offset:int =0):
        self.get_property('mappings')[name] =  {'base': base, 'size': size, 'x': x, 'y': y, 'remove_offset':remove_offset}

    def add_router(self, x: int, y: int):
        """Instantiate a router in the grid.

        Parameters
        ----------
        x: int
            X position of the router in the grid
        y: int
            Y position of the router in the grid
        """
        self.get_property('routers').append([x, y])

    def add_network_interface(self, x: int, y: int):
        """Instantiate a network interface in the grid.

        A network interface should be instantiated at every node where a burst can be injected,
        typically next to each cluster.

        Parameters
        ----------
        x: int
            X position of the network interface in the grid
        y: int
            Y position of the network interface in the grid
        """
        self.get_property('network_interfaces').append([x, y])

    def o_NARROW_MAP(self, itf: gvsoc.systree.SlaveItf, base: int, size: int,
            x: int, y: int, name: str=None, rm_base: bool=False, remove_offset:int =0):
        """Binds the output of a node to a target, associated to a memory-mapped region.

        Parameters
        ----------
        itf: gvsoc.systree.SlaveItf
            Slave interface where requests matching the memory-mapped region will be sent.
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
        if name is None:
            name = itf.component.name
        if rm_base and remove_offset == 0:
            remove_offset =base
        self.__add_mapping(f"narrow_{name}", base=base, size=size, x=x, y=y, remove_offset=remove_offset)
        self.itf_bind(f"narrow_{name}", itf, signature='io')



    def o_WIDE_MAP(self, itf: gvsoc.systree.SlaveItf, base: int, size: int,
            x: int, y: int, name: str=None, rm_base: bool=False, remove_offset:int =0):
        """Binds the output of a node to a target, associated to a memory-mapped region.

        Parameters
        ----------
        itf: gvsoc.systree.SlaveItf
            Slave interface where requests matching the memory-mapped region will be sent.
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
        if name is None:
            name = itf.component.name
        if rm_base and remove_offset == 0:
            remove_offset =base
        self.__add_mapping(f"wide_{name}", base=base, size=size, x=x, y=y, remove_offset=remove_offset)
        self.itf_bind(f"wide_{name}", itf, signature='io')

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





class FlooNocClusterGridNarrowWide(FlooNoc2dMeshNarrowWide):
    """FlooNoc instance for a grid of clusters

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
    nb_x_clusters: int
        Number of clusters on the X direction. This should not include the targets on the borders.
    nb_y_clusters: int
        Number of clusters on the Y direction. This should not include the targets on the borders.
    """
    def __init__(self, parent: gvsoc.systree.Component, name, wide_width: int,narrow_width:int, nb_x_clusters: int,
            nb_y_clusters, router_input_queue_size=2, ni_outstanding_reqs: int=2):
        # The total grid contains 1 more node on each direction for the targets
        super().__init__(parent, name, wide_width=wide_width, narrow_width=narrow_width, dim_x=nb_x_clusters+2, dim_y=nb_y_clusters+2, router_input_queue_size=router_input_queue_size, ni_outstanding_reqs=ni_outstanding_reqs)

        for tile_x in range(0, nb_x_clusters+2):
            for tile_y in range(0, nb_y_clusters+2):
                # Add 1 as clusters, routers and network_interfaces are in the central part
                self.add_router(tile_x, tile_y) # Add a router at each cluster
        for tile_x in range(0, nb_x_clusters+2):
            for tile_y in range(0, nb_y_clusters+2):
                # Add a NI at each node, excluding the corners, because it also (once finished) acts as an output to the targets
                if not (tile_x == 0 and tile_y == 0) or (tile_x == 0 and tile_y == nb_y_clusters+1) or (tile_x == nb_x_clusters+1 and tile_y == 0) or (tile_x == nb_x_clusters+1 and tile_y == nb_y_clusters+1):
                    self.add_network_interface(tile_x, tile_y)

    def i_CLUSTER_NARROW_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        """Returns the input port of a cluster tile.

        The cluster can inject requests to the noc using this interface. The noc will then
        forward it to the right target.

        Parameters
        ----------
        x: int
            The x position of the cluster in the grid
        y: int
            The y position of the cluster in the grid

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return self.i_NARROW_INPUT(x+1, y+1)

    def i_CLUSTER_WIDE_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        """Returns the input port of a cluster tile.

        The cluster can inject requests to the noc using this interface. The noc will then
        forward it to the right target.

        Parameters
        ----------
        x: int
            The x position of the cluster in the grid
        y: int
            The y position of the cluster in the grid

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return self.i_WIDE_INPUT(x+1, y+1)


class FlooNoc2dMesh(FlooNoc2dMeshNarrowWide):
    pass

class FlooNocClusterGrid(FlooNocClusterGridNarrowWide):
    pass
