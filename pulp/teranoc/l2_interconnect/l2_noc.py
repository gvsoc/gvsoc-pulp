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
from pulp.teranoc.l2_interconnect.floonoc import FlooNocClusterGridNarrowWide

class L2_noc(FlooNocClusterGridNarrowWide):
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

        super(L2_noc, self).__init__(parent, name, wide_width=width, narrow_width=0, nb_x_clusters=nb_x_groups, nb_y_clusters=nb_y_groups, \
                                        router_input_queue_size=128, ni_outstanding_reqs=128)

    def add_mapping(self, base: int, size: int, x: int, y: int, name: str=None, rm_base: bool=False, remove_offset:int =0):
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
        if rm_base and remove_offset == 0:
            remove_offset =base
        self._FlooNoc2dMeshNarrowWide__add_mapping(f"wide_{name}", base=base, size=size, x=x, y=y, remove_offset=remove_offset)