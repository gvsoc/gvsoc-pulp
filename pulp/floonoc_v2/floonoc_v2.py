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


class FlooNocV2Direction(IntEnum):
    RIGHT = -4
    LEFT = -3
    UP = -2
    DOWN = -1


class FlooNocV22dMeshNarrowWide(gvsoc.systree.Component):
    """FlooNoC v2 (io v2 protocol) instance for a 2D mesh.

    Mirrors the v1 FlooNoc2dMeshNarrowWide generator, but instantiates the v2
    model under gvsoc/pulp/pulp/floonoc_v2/. Ports speak the v2 io protocol
    (vp/itf/io_v2.hpp) — burst beats with is_first/is_last/burst_id and the
    retry() deny handshake.
    """
    def __init__(self, parent: gvsoc.systree.Component, name, narrow_width: int, wide_width:int,
            dim_x: int, dim_y:int, ni_outstanding_reqs: int=8, router_input_queue_size: int=2):
        super().__init__(parent, name)

        self.add_sources([
            'pulp/floonoc_v2/floonoc_v2.cpp',
            'pulp/floonoc_v2/floonoc_router_v2.cpp',
            'pulp/floonoc_v2/floonoc_network_interface_v2.cpp',
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
        self.get_property('routers').append([x, y])

    def add_network_interface(self, x: int, y: int):
        self.get_property('network_interfaces').append([x, y])

    def o_NARROW_MAP(self, itf: gvsoc.systree.SlaveItf, base: int, size: int,
            x: int, y: int, name: str=None, rm_base: bool=False, remove_offset:int =0):
        if name is None:
            name = itf.component.name
        if rm_base and remove_offset == 0:
            remove_offset = base
        self.__add_mapping(f"narrow_{name}", base=base, size=size, x=x, y=y, remove_offset=remove_offset)
        self.itf_bind(f"ni_narrow_{x}_{y}", itf, signature='io_v2')

    def o_WIDE_BIND(self, itf: gvsoc.systree.SlaveItf, x: int, y: int):
        self.itf_bind(f"ni_wide_{x}_{y}", itf, signature='io_v2')

    def o_NARROW_BIND(self, itf: gvsoc.systree.SlaveItf, x: int, y: int):
        self.itf_bind(f"ni_narrow_{x}_{y}", itf, signature='io_v2')

    def o_MAP_DIR(self, base: int, size: int, dir: FlooNocV2Direction, name: str,
            rm_base: bool=False, remove_offset:int =0):
        if rm_base and remove_offset == 0:
            remove_offset = base
        self.__add_mapping(f"ni_{name}", base=base, size=size, x=dir, y=0, remove_offset=remove_offset)

    def o_MAP(self, base: int, size: int,
            x: int, y: int,
            rm_base: bool=False, remove_offset:int =0):
        if rm_base and remove_offset == 0:
            remove_offset = base
        self.__add_mapping(f"ni_{x}_{y}", base=base, size=size, x=x, y=y, remove_offset=remove_offset)

    def i_NARROW_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'narrow_input_{x}_{y}', signature='io_v2')

    def i_WIDE_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'wide_input_{x}_{y}', signature='io_v2')


class FlooNocV2ClusterGridNarrowWide(FlooNocV22dMeshNarrowWide):
    """FlooNoC v2 instance for a grid of clusters (mirrors v1's variant)."""
    def __init__(self, parent: gvsoc.systree.Component, name, wide_width: int, narrow_width:int, nb_x_clusters: int,
            nb_y_clusters, router_input_queue_size=2, ni_outstanding_reqs: int=2):
        super().__init__(parent, name, wide_width=wide_width, narrow_width=narrow_width,
            dim_x=nb_x_clusters+2, dim_y=nb_y_clusters+2,
            router_input_queue_size=router_input_queue_size,
            ni_outstanding_reqs=ni_outstanding_reqs)

        for tile_x in range(0, nb_x_clusters):
            for tile_y in range(0, nb_y_clusters):
                self.add_router(tile_x+1, tile_y+1)
        for tile_x in range(0, nb_x_clusters+2):
            for tile_y in range(0, nb_y_clusters+2):
                if not ((tile_x == 0 and tile_y == 0) or (tile_x == 0 and tile_y == nb_y_clusters+1) \
                        or (tile_x == nb_x_clusters+1 and tile_y == 0) or \
                        (tile_x == nb_x_clusters+1 and tile_y == nb_y_clusters+1)):
                    self.add_network_interface(tile_x, tile_y)

    def i_CLUSTER_NARROW_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        return self.i_NARROW_INPUT(x+1, y+1)

    def i_CLUSTER_WIDE_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        return self.i_WIDE_INPUT(x+1, y+1)
