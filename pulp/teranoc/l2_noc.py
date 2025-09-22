#
# Copyright (C) 2025 ETH Zurich and University of Bologna
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
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)

import gvsoc.systree
from pulp.floonoc.floonoc import FlooNoc2dMeshNarrowWide

class L2_noc(FlooNoc2dMeshNarrowWide):
    """
    FlooNoc instance for L2 inter-group communication

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    width: int
        The width in bytes of the interconnect. This gives the number of bytes/cycles each node can
        route.
    nb_x_groups: int
        Number of groups on the X direction.
    nb_y_groups: int
        Number of groups on the Y direction.
    ni_outstanding_reqs: int
        Number of outstanding requests each network interface can inject to the routers. This should
        be increased when the size of the noc increases.
    router_input_queue_size: int
        Size of the routers input queues. This gives the number of requests which can be buffered
        before the source output queue is stalled.
    """
    def __init__(self, parent: gvsoc.systree.Component, name, width: int, nb_x_groups: int,
            nb_y_groups: int, ni_outstanding_reqs: int=2, router_input_queue_size: int=2):

        super(L2_noc, self).__init__(parent, name, width, 0, dim_x=nb_x_groups, dim_y=nb_y_groups,
                ni_outstanding_reqs=ni_outstanding_reqs, router_input_queue_size=router_input_queue_size)

        for tile_x in range(0, nb_x_groups):
            for tile_y in range(0, nb_y_groups):
                self.add_router(tile_x, tile_y)
                self.add_network_interface(tile_x, tile_y)