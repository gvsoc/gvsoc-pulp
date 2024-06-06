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

class FlooNoc2dMesh(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name, width: int,
            dim_x: int, dim_y:int, ni_outstanding_reqs: int=8, router_input_queue_size: int=2):
        super(FlooNoc2dMesh, self).__init__(parent, name)

        self.add_sources([
            'pulp/floonoc/floonoc.cpp',
            'pulp/floonoc/floonoc_router.cpp',
            'pulp/floonoc/floonoc_network_interface.cpp',
        ])

        self.add_property('mappings', {})
        self.add_property('routers', [])
        self.add_property('network_interfaces', [])
        self.add_property('ni_outstanding_reqs', ni_outstanding_reqs)
        self.add_property('width', width/8)
        self.add_property('dim_x', dim_x)
        self.add_property('dim_y', dim_y)
        self.add_property('router_input_queue_size', router_input_queue_size)

    def __add_mapping(self, name: str, base: int, size: int, x: int, y: int):
        self.get_property('mappings')[name] =  {'base': base, 'size': size, 'x': x, 'y': y}

    def add_router(self, x: int, y: int):
        self.get_property('routers').append([x, y])

    def add_network_interface(self, x: int, y: int):
        self.get_property('network_interfaces').append([x, y])

    def o_MAP(self, itf: gvsoc.systree.SlaveItf, base: int, size: int,
            x: int, y: int):
        name = itf.component.name
        self.__add_mapping(name, base=base, size=size, x=x, y=y)
        self.itf_bind(name, itf, signature='io')

    def i_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'input_{x}_{y}', signature='io')


class FlooNocClusterTiles(FlooNoc2dMesh):
    def __init__(self, parent: gvsoc.systree.Component, name, width: int, nb_x_tiles, nb_y_tiles):
        super(FlooNocClusterTiles, self).__init__(parent, name, width, dim_x=nb_x_tiles+2, dim_y=nb_y_tiles+2)

        for tile_x in range(0, nb_x_tiles):
            for tile_y in range(0, nb_y_tiles):
                self.add_router(tile_x+1, tile_y+1)
                self.add_network_interface(tile_x+1, tile_y+1)

    def i_TILE_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        return self.i_INPUT(x+1, y+1)

