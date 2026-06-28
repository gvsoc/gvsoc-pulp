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


class FlooNoc2dMeshNarrow(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name, narrow_width: int,
            dim_x: int, dim_y:int, router_input_queue_size: int=2, router_output_queue_size: int=2):
        super().__init__(parent, name)

        self.add_sources([
            'pulp/teranoc/l1_interconnect/floonoc.cpp',
            'pulp/teranoc/l1_interconnect/floonoc_router.cpp',
            'pulp/teranoc/l1_interconnect/floonoc_network_interface.cpp',
        ])

        self.add_property('narrow_width', narrow_width)
        self.add_property('dim_x', dim_x)
        self.add_property('dim_y', dim_y)
        self.add_property('router_input_queue_size', router_input_queue_size)
        self.add_property('router_output_queue_size', router_output_queue_size)

    def o_MAP(self, itf: gvsoc.systree.SlaveItf, x: int, y: int):
        """Binds the output of a node to a target.

        Parameters
        ----------
        itf: gvsoc.systree.SlaveItf
            Slave interface to which requests reaching node (x, y) are forwarded.
        x: int
            X position of the target in the grid.
        y: int
            Y position of the target in the grid.
        """
        self.itf_bind(f"out_{x}_{y}", itf, signature='io')

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
        return gvsoc.systree.SlaveItf(self, f'in_{x}_{y}', signature='io')
