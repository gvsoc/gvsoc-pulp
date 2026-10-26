#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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

# Author: Chi Zhang <chizhang@ethz.ch>

from pulp.floonoc_flex.floonoc_flex import FlooNocFlex
import gvsoc.systree

class McPoolMeshNoc(FlooNocFlex):
    """FlooNoc Flex instance for a grid of clusters

    This instantiates a 2D mesh of FlooNoc Flex routers for a grid of clusters, with XY routing.
    It contains:
     - one router and one network interface for each cluster
     - one network interface on each border position, next to the border routers, for the targets
       on the edges (HBM)
    The wide network carries the data traffic and the narrow one the synchronization traffic.

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    narrow_width: int
        The width in bytes of the narrow network.
    wide_width: int
        The width in bytes of the wide network.
    nb_x_clusters: int
        Number of clusters on the X direction.
    nb_y_clusters: int
        Number of clusters on the Y direction.
    ni_outstanding_reqs: int
        Number of outstanding requests each network interface can inject to the routers. This should
        be increased when the size of the noc increases.
    router_input_queue_size: int
        Size of the routers input queues. This gives the number of requests which can be buffered
        before the source output queue is stalled.
    link_latency: int
        Latency in cycles of every link.
    """
    def __init__(self, parent: gvsoc.systree.Component, name, narrow_width: int, wide_width: int,
            nb_x_clusters: int, nb_y_clusters: int, ni_outstanding_reqs: int=2,
            router_input_queue_size: int=2, link_latency: int=1):
        super(McPoolMeshNoc, self).__init__(parent, name, narrow_width=narrow_width,
            wide_width=wide_width, ni_outstanding_reqs=ni_outstanding_reqs,
            router_input_queue_size=router_input_queue_size)

        # Node names follow the floogen convention, so that the dimension-order routing can
        # parse the router coordinates from them
        node_to_id = {}
        def add_node(node_name):
            node_to_id[node_name] = len(node_to_id)
            return node_to_id[node_name]

        routers = {}
        for y in range(nb_y_clusters):
            for x in range(nb_x_clusters):
                routers[(x, y)] = add_node(f'router_{x}_{y}')

        # Network interface of each node, with the router it is attached to
        nis = {}
        for y in range(nb_y_clusters):
            for x in range(nb_x_clusters):
                nis[f'cluster_{x}_{y}_ni'] = routers[(x, y)]
        for y in range(nb_y_clusters):
            nis[f'west_{y}_ni'] = routers[(0, y)]
            nis[f'east_{y}_ni'] = routers[(nb_x_clusters - 1, y)]
        for x in range(nb_x_clusters):
            nis[f'south_{x}_ni'] = routers[(x, 0)]
            nis[f'north_{x}_ni'] = routers[(x, nb_y_clusters - 1)]

        links = []
        for (x, y), router_id in routers.items():
            if x + 1 < nb_x_clusters:
                links.append((router_id, routers[(x + 1, y)]))
            if y + 1 < nb_y_clusters:
                links.append((router_id, routers[(x, y + 1)]))
        for ni_name, router_id in nis.items():
            links.append((add_node(ni_name), router_id))

        for router_id in routers.values():
            nb_ports = sum(router_id in link for link in links)
            self.add_router(router_id, num_queues=nb_ports)
        for ni_name in nis.keys():
            self.add_network_interface(node_to_id[ni_name])
        for src, dst in links:
            self.add_link(src, dst, latency=link_latency)

        self.add_property('nb_nodes', len(node_to_id))
        self.id_map = node_to_id
        self.generate_routing_tables_dimension_order(node_to_id, first_axis='x')

    def cluster_node(self, x: int, y: int) -> int:
        """Returns the node ID of the network interface of a cluster."""
        return self.id_map[f'cluster_{x}_{y}_ni']

    def edge_node(self, edge: str, index: int) -> int:
        """Returns the node ID of the network interface of an edge target.

        edge is one of west, north, east or south, and index is the position along the edge.
        """
        return self.id_map[f'{edge}_{index}_ni']

    def o_WIDE_MAP(self, itf: gvsoc.systree.SlaveItf, base: int, size: int, node_id: int):
        """Binds a target of the wide network to a node, associated to a memory-mapped region.

        The base address is removed from the requests going to the target.
        """
        name = itf.component.name
        self.get_property('mappings')[f'wide_{name}'] = {
            'base': base, 'size': size, 'node_id': node_id, 'remove_offset': base
        }
        self.o_WIDE_BIND(itf, node_id)

    def o_NARROW_MAP(self, itf: gvsoc.systree.SlaveItf, base: int, size: int, node_id: int):
        """Binds a target of the narrow network to a node, associated to a memory-mapped region.

        The base address is removed from the requests going to the target.
        """
        super().o_NARROW_MAP(itf, base=base, size=size, node_id=node_id, rm_base=True)

    def i_CLUSTER_WIDE_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        """Returns the wide input port of a cluster, to inject data requests."""
        return self.i_WIDE_INPUT(self.cluster_node(x, y))

    def i_CLUSTER_NARROW_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        """Returns the narrow input port of a cluster, to inject synchronization requests."""
        return self.i_NARROW_INPUT(self.cluster_node(x, y))
