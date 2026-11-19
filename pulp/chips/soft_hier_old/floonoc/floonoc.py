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
from typing import ClassVar
from config_tree import Config, cfg_field


class FlooNocMappingConfig(Config):
    """One memory-mapped target of the noc. The Config ``name`` field carries the name of the
    master port where requests to the target are sent."""

    _defer_parent_init: ClassVar[bool] = True

    base: int = cfg_field(default=0, fmt="hex", desc="Base address of the target")
    size: int = cfg_field(default=0, fmt="hex", desc="Size of the target")
    x: int = cfg_field(default=0, desc="X position of the target in the grid")
    y: int = cfg_field(default=0, desc="Y position of the target in the grid")


class FlooNocNodeConfig(Config):
    """Position of one node (network interface) in the grid."""

    _defer_parent_init: ClassVar[bool] = True

    x: int = cfg_field(default=0, desc="X position in the grid")
    y: int = cfg_field(default=0, desc="Y position in the grid")


class FlooNocConfig(Config):
    """Configuration of the SoftHier (old) FlooNoc 2D mesh."""

    mappings: list[FlooNocMappingConfig] = cfg_field(default_factory=list, desc=(
        "Memory-mapped targets, sorted by name"))
    network_interfaces: list[FlooNocNodeConfig] = cfg_field(default_factory=list, desc=(
        "Positions of the network interfaces, a router is also instantiated at each of them"))
    ni_outstanding_reqs: int = cfg_field(default=0, dump=True, desc=(
        "Number of outstanding requests each network interface can inject to the routers"))
    width: int = cfg_field(default=0, dump=True, desc="Width in bytes of the interconnect")
    dim_x: int = cfg_field(default=0, dump=True, desc="X dimension of the grid, including the borders")
    dim_y: int = cfg_field(default=0, dump=True, desc="Y dimension of the grid, including the borders")
    router_input_queue_size: int = cfg_field(default=0, dump=True, desc="Size of the routers input queues")
    atomics: int = cfg_field(default=0, dump=True, desc="1 to support atomics")
    collective: int = cfg_field(default=0, dump=True, desc="1 to support collective operations")
    interleave_enable: int = cfg_field(default=0, dump=True, desc="1 to enable address interleaving")
    interleave_region_base: int = cfg_field(default=0, fmt="hex", dump=True, desc="Base of the interleaved region")
    interleave_region_size: int = cfg_field(default=0, fmt="hex", dump=True, desc="Size of the interleaved region")
    interleave_granularity: int = cfg_field(default=0, dump=True, desc="Interleaving granularity")
    interleave_bit_start: int = cfg_field(default=0, dump=True, desc="First address bit of the interleaving")
    interleave_bit_width: int = cfg_field(default=0, dump=True, desc="Number of address bits of the interleaving")
    edge_node_alias: int = cfg_field(default=0, dump=True, desc="1 to alias the edge nodes")
    edge_node_alias_start_bit: int = cfg_field(default=0, dump=True, desc="First address bit of the edge node alias")


class FlooNoc2dMesh(gvsoc.systree.Component):
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
    def __init__(self, parent: gvsoc.systree.Component, name, width: int,
            dim_x: int, dim_y:int, ni_outstanding_reqs: int=8, router_input_queue_size: int=2, atomics: int=0, collective: int=0,
            edge_node_alias: int=0, edge_node_alias_start_bit: int=48,
            interleave_enable: int=0, interleave_region_base: int=0, interleave_region_size: int=0, interleave_granularity: int=0, interleave_bit_start: int=0, interleave_bit_width: int=0):
        self.noc_config = FlooNocConfig(ni_outstanding_reqs=ni_outstanding_reqs,
            width=int(width), dim_x=dim_x, dim_y=dim_y,
            router_input_queue_size=router_input_queue_size, atomics=atomics,
            collective=collective, interleave_enable=interleave_enable,
            interleave_region_base=interleave_region_base,
            interleave_region_size=interleave_region_size,
            interleave_granularity=interleave_granularity,
            interleave_bit_start=interleave_bit_start, interleave_bit_width=interleave_bit_width,
            edge_node_alias=edge_node_alias, edge_node_alias_start_bit=edge_node_alias_start_bit)
        # Router positions, only kept for the generator: the model instantiates one router at
        # each network interface
        self.routers = []

        super(FlooNoc2dMesh, self).__init__(parent, name, config=self.noc_config)

        self.add_sources([
            'pulp/chips/soft_hier_old/floonoc/floonoc.cpp',
            'pulp/chips/soft_hier_old/floonoc/floonoc_router.cpp',
            'pulp/chips/soft_hier_old/floonoc/floonoc_network_interface.cpp',
        ])


    def __add_mapping(self, name: str, base: int, size: int, x: int, y: int):
        # Same semantics as the former name-indexed dict: a name mapped again replaces its
        # entry, and entries are kept sorted by name, which is the order the model uses.
        mappings = [mapping for mapping in self.noc_config.mappings if mapping.name != name]
        mappings.append(FlooNocMappingConfig(name=name, base=base, size=size, x=x, y=y))
        mappings.sort(key=lambda mapping: mapping.name)
        self.noc_config.mappings = mappings

    def add_router(self, x: int, y: int):
        """Instantiate a router in the grid.

        Parameters
        ----------
        x: int
            X position of the router in the grid
        y: int
            Y position of the router in the grid
        """
        self.routers.append([x, y])

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
        self.noc_config.network_interfaces.append(FlooNocNodeConfig(x=x, y=y))

    def o_MAP(self, itf: gvsoc.systree.SlaveItf, base: int, size: int,
            x: int, y: int):
        """Binds the output of a node to a target, associated to a memory-mapped region.

        Parameters
        ----------
        itf: gvsoc.systree.SlaveItf
            Slave interface where requests matching the memory-mapped region will be sent.
        x: int
            X position of the target in the grid
        y: int
            Y position of the target in the grid
        """
        name = itf.component.name
        self.__add_mapping(name, base=base, size=size, x=x, y=y)
        self.itf_bind(name, itf, signature='io')

    def i_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        """Returns the input port of a node.

        Requests can be injected to the noc using this interface. The noc will then
        forward it to the right target.

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, f'input_{x}_{y}', signature='io')


class FlooNocClusterGrid(FlooNoc2dMesh):
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
    def __init__(self, parent: gvsoc.systree.Component, name, width: int, nb_x_clusters: int,
            nb_y_clusters):
        # The total grid contains 1 more node on each direction for the targets
        super(FlooNocClusterGrid, self).__init__(parent, name, width, dim_x=nb_x_clusters+2, dim_y=nb_y_clusters+2)

        for tile_x in range(0, nb_x_clusters):
            for tile_y in range(0, nb_y_clusters):
                # Add 1 as clusters, routers and network_interfaces are in the central part
                self.add_router(tile_x+1, tile_y+1)
                self.add_network_interface(tile_x+1, tile_y+1)

    def i_CLUSTER_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        """Returns the input port of a cluster tile.

        The cluster can inject requests to the noc using this interface. The noc will then
        forward it to the right target.

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return self.i_INPUT(x+1, y+1)

