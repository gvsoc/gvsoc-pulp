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
from gvsoc.signature import IoV2Beat


class FlooNocV2Direction(IntEnum):
    RIGHT = -4
    LEFT = -3
    UP = -2
    DOWN = -1


# Direction indices of the router link ports. Must match RouterV2::DIR_* in
# floonoc_router_v2.hpp.
DIR_RIGHT = 0
DIR_LEFT = 1
DIR_UP = 2
DIR_DOWN = 3
DIR_LOCAL = 4

_DIR_NAMES = ['right', 'left', 'up', 'down', 'local']
_DIR_OPPOSITE = {DIR_RIGHT: DIR_LEFT, DIR_LEFT: DIR_RIGHT, DIR_UP: DIR_DOWN, DIR_DOWN: DIR_UP}

# Network indices of the NI link ports. Must match NetworkInterfaceV2::NW_* in
# floonoc_network_interface_v2.hpp.
NW_REQ = 0
NW_RSP = 1
NW_WIDE = 2

_NW_NAMES = ['req', 'rsp', 'wide']
_NW_ROUTER_PREFIXES = ['req_router_', 'rsp_router_', 'wide_router_']


class FloonocRouterV2(gvsoc.systree.Component):
    """One FlooNoC mesh router (one physical network at one tile).

    Five 'floonoc_link' inputs and five outputs, indexed by direction. Ports of
    absent neighbours are simply left unbound.
    """

    def __init__(self, parent: gvsoc.systree.Component, name, x: int, y: int,
            dim_x: int, dim_y: int, queue_size: int):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/soft_hier_old/floonoc_v2/floonoc_router_v2.cpp'])

        self.add_property('x', x)
        self.add_property('y', y)
        self.add_property('dim_x', dim_x)
        self.add_property('dim_y', dim_y)
        self.add_property('router_input_queue_size', queue_size)

    def i_INPUT(self, dir: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'input_{_DIR_NAMES[dir]}', signature='floonoc_link')

    def o_OUTPUT(self, dir: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'output_{_DIR_NAMES[dir]}', itf, signature='floonoc_link')


class FloonocNetworkInterfaceV2(gvsoc.systree.Component):
    """One FlooNoC network interface (mesh entry/exit point at one position).

    External ports speak the v2 io protocol; three 'floonoc_link' output ports
    inject into the routers of the req/rsp/wide networks and three input ports
    receive what they deliver.
    """

    def __init__(self, parent: gvsoc.systree.Component, name, x: int, y: int,
            narrow_width: int, wide_width: int, ni_outstanding_reqs: int,
            max_burst_size: int=4096):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/soft_hier_old/floonoc_v2/floonoc_network_interface_v2.cpp'])

        self.narrow_width = narrow_width
        self.wide_width = wide_width

        self.add_property('x', x)
        self.add_property('y', y)
        self.add_property('narrow_width', narrow_width)
        self.add_property('wide_width', wide_width)
        self.add_property('ni_outstanding_reqs', ni_outstanding_reqs)
        # Largest input burst the NI accepts, and the boundary a burst may not
        # cross (the AXI 4 KB rule). Guarantees a burst targets a single mesh
        # position, so a wormhole packet is always single-destination. Checked
        # against the memory map at construction and against each request at
        # runtime (asserts builds). 0 disables the checks.
        self.add_property('max_burst_size', max_burst_size)

    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature=IoV2Beat(self.narrow_width))

    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input', signature=IoV2Beat(self.wide_width))

    def o_NARROW_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_output', itf, signature=IoV2Beat(self.narrow_width))

    def o_WIDE_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_output', itf, signature=IoV2Beat(self.wide_width))

    def i_LINK(self, nw: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'{_NW_NAMES[nw]}_link_in', signature='floonoc_link')

    def o_LINK(self, nw: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'{_NW_NAMES[nw]}_link_out', itf, signature='floonoc_link')


class FloonocTileV2(gvsoc.systree.Component):
    """Composite grouping the routers and the NI of one mesh position.

    Pure grouping component with no behavior of its own: it only makes the
    GUI model view and the traces show the mesh as one block per tile
    (e.g. one per compute unit) instead of a flat sea of routers and NIs.
    Mesh links and external NI ports are passed through tile-level virtual
    ports, which the engine flattens away at binding time — the runtime
    connections (and so the timing) are identical to the ungrouped layout.

    Created by FlooNocV2MeshFabric when add_router() /
    add_network_interface() are called with a tile name; one tile per mesh
    position.
    """

    def __init__(self, parent: gvsoc.systree.Component, name, x: int, y: int):
        super().__init__(parent, name)

        self.x = x
        self.y = y
        # Filled by the mesh fabric.
        self.routers = None
        self.ni = None


class FlooNocV2MeshFabric:
    """FlooNoC v2 mesh builder.

    Plain helper (not a component): instantiates the routers, NIs and optional
    grouping tiles of a 2D mesh as children of ``container``, and binds the
    mesh together in finalize(). Two ways to use it:

    - Directly, with the surrounding component (e.g. the chip) as container:
      the mesh elements sit at the same hierarchy level as the units they
      serve, with no wrapping composite. The container must call
      ``fabric.finalize()`` from its own ``finalize()`` hook.

    - Through FlooNocV22dMeshNarrowWide, which wraps the fabric in a composite
      exposing the classic per-position ports and address-map API.
    """

    def __init__(self, container: gvsoc.systree.Component, narrow_width: int, wide_width: int,
            dim_x: int, dim_y: int, ni_outstanding_reqs: int=8, router_input_queue_size: int=2,
            mappings: dict=None, max_burst_size: int=4096):
        self.container = container
        self.narrow_width = narrow_width
        self.wide_width = wide_width
        self.dim_x = dim_x
        self.dim_y = dim_y
        self.ni_outstanding_reqs = ni_outstanding_reqs
        self.router_input_queue_size = router_input_queue_size
        self.max_burst_size = max_burst_size

        # Memory map, name -> {base, size, x, y, remove_offset}. Distributed to
        # every NI in finalize(), so entries can keep being added after
        # construction.
        self.mappings = mappings if mappings is not None else {}

        # (x, y) -> [req, rsp, wide] router components / (NI component, tile).
        # Children are created eagerly in add_router()/add_network_interface();
        # only the link bindings are deferred to finalize().
        self._routers = {}
        self._nis = {}
        # Optional per-position grouping composites: tile name -> FloonocTileV2
        # and (x, y) -> tile of the routers there. Tile-level pass-through
        # ports are created eagerly (with the children) because composite
        # virtual ports must exist before the tree is built; the bindings
        # between tiles are deferred to finalize() like everything else.
        self._tiles = {}
        self._router_tiles = {}

    def add_mapping(self, name: str, base: int, size: int, x: int, y: int, remove_offset: int=0):
        self.mappings[name] = {'base': base, 'size': size, 'x': x, 'y': y,
            'remove_offset': remove_offset}

    def _get_tile(self, name: str, x: int, y: int) -> FloonocTileV2:
        tile = self._tiles.get(name)
        if tile is None:
            tile = FloonocTileV2(self.container, name, x, y)
            self._tiles[name] = tile
        elif (tile.x, tile.y) != (x, y):
            raise RuntimeError(f'Tile {name} is at position ({tile.x}, {tile.y}), '
                f'cannot also be used at ({x}, {y}) — one tile per mesh position')
        return tile

    def add_router(self, x: int, y: int, tile: str=None):
        if (x, y) in self._routers:
            raise RuntimeError(f'Routers already added at position ({x}, {y})')
        if (x, y) in self._nis:
            raise RuntimeError(f'add_router must be called before '
                f'add_network_interface for position ({x}, {y})')

        if tile is None:
            self._routers[(x, y)] = [
                FloonocRouterV2(self.container, f'{_NW_ROUTER_PREFIXES[nw]}{x}_{y}', x=x, y=y,
                    dim_x=self.dim_x, dim_y=self.dim_y,
                    queue_size=self.router_input_queue_size)
                for nw in range(len(_NW_NAMES))
            ]
            return

        tile_comp = self._get_tile(tile, x, y)
        routers = [
            FloonocRouterV2(tile_comp, f'{_NW_NAMES[nw]}_router', x=x, y=y,
                dim_x=self.dim_x, dim_y=self.dim_y,
                queue_size=self.router_input_queue_size)
            for nw in range(len(_NW_NAMES))
        ]
        self._routers[(x, y)] = routers
        self._router_tiles[(x, y)] = tile_comp
        tile_comp.routers = routers

        # Tile-level pass-throughs of the mesh link ports, for every direction
        # that can face another position of the mesh. finalize() binds tiles
        # together through these.
        neighbours = {DIR_RIGHT: (x+1, y), DIR_LEFT: (x-1, y), DIR_UP: (x, y+1), DIR_DOWN: (x, y-1)}
        for dir, (n_x, n_y) in neighbours.items():
            if 0 <= n_x < self.dim_x and 0 <= n_y < self.dim_y:
                for nw in range(len(_NW_NAMES)):
                    tile_comp.itf_bind(f'{_NW_NAMES[nw]}_input_{_DIR_NAMES[dir]}',
                        routers[nw].i_INPUT(dir), signature='floonoc_link',
                        composite_bind=True)
                    routers[nw].o_OUTPUT(dir, gvsoc.systree.SlaveItf(tile_comp,
                        f'{_NW_NAMES[nw]}_output_{_DIR_NAMES[dir]}', signature='floonoc_link'))

    def add_network_interface(self, x: int, y: int, tile: str=None):
        tile_comp = self._get_tile(tile, x, y) if tile is not None else None

        if (x, y) in self._routers and self._router_tiles.get((x, y)) is not tile_comp:
            raise RuntimeError(f'Routers and NI at position ({x}, {y}) must use '
                'the same tile')

        if tile_comp is None:
            ni = FloonocNetworkInterfaceV2(self.container, f'ni_{x}_{y}', x=x, y=y,
                narrow_width=self.narrow_width, wide_width=self.wide_width,
                ni_outstanding_reqs=self.ni_outstanding_reqs,
                max_burst_size=self.max_burst_size)
        else:
            ni = FloonocNetworkInterfaceV2(tile_comp, 'ni', x=x, y=y,
                narrow_width=self.narrow_width, wide_width=self.wide_width,
                ni_outstanding_reqs=self.ni_outstanding_reqs,
                max_burst_size=self.max_burst_size)
            tile_comp.ni = ni

            # Chain the NI's external ports through the tile.
            tile_comp.itf_bind('narrow_input', ni.i_NARROW_INPUT(),
                signature=IoV2Beat(self.narrow_width), composite_bind=True)
            tile_comp.itf_bind('wide_input', ni.i_WIDE_INPUT(),
                signature=IoV2Beat(self.wide_width), composite_bind=True)
            ni.o_NARROW_OUTPUT(gvsoc.systree.SlaveItf(tile_comp, 'narrow_output',
                signature=IoV2Beat(self.narrow_width)))
            ni.o_WIDE_OUTPUT(gvsoc.systree.SlaveItf(tile_comp, 'wide_output',
                signature=IoV2Beat(self.wide_width)))

            # Border NI (no router in this tile): its mesh links leave the tile,
            # so they also need tile-level pass-throughs.
            if self._router_tiles.get((x, y)) is not tile_comp:
                for nw in range(len(_NW_NAMES)):
                    tile_comp.itf_bind(f'{_NW_NAMES[nw]}_ni_link_in', ni.i_LINK(nw),
                        signature='floonoc_link', composite_bind=True)
                    ni.o_LINK(nw, gvsoc.systree.SlaveItf(tile_comp,
                        f'{_NW_NAMES[nw]}_ni_link_out', signature='floonoc_link'))

        self._nis[(x, y)] = (ni, tile_comp)

    def i_NARROW_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        """Bindable narrow input of the NI at (x, y) — the tile port when tiled."""
        ni, tile = self._nis[(x, y)]
        if tile is not None:
            return gvsoc.systree.SlaveItf(tile, 'narrow_input',
                signature=IoV2Beat(self.narrow_width))
        return ni.i_NARROW_INPUT()

    def i_WIDE_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        """Bindable wide input of the NI at (x, y) — the tile port when tiled."""
        ni, tile = self._nis[(x, y)]
        if tile is not None:
            return gvsoc.systree.SlaveItf(tile, 'wide_input',
                signature=IoV2Beat(self.wide_width))
        return ni.i_WIDE_INPUT()

    def o_NARROW_BIND(self, itf: gvsoc.systree.SlaveItf, x: int, y: int):
        """Bind the narrow output of the NI at (x, y) — through its tile when tiled."""
        ni, tile = self._nis[(x, y)]
        comp = tile if tile is not None else ni
        comp.itf_bind('narrow_output', itf, signature=IoV2Beat(self.narrow_width))

    def o_WIDE_BIND(self, itf: gvsoc.systree.SlaveItf, x: int, y: int):
        """Bind the wide output of the NI at (x, y) — through its tile when tiled."""
        ni, tile = self._nis[(x, y)]
        comp = tile if tile is not None else ni
        comp.itf_bind('wide_output', itf, signature=IoV2Beat(self.wide_width))

    def _ni_router_pos(self, x: int, y: int):
        """Position of the router an NI attaches to.

        NIs on tiles without a router (mesh borders) attach to the nearest
        adjacent router: x+1 first, then x-1, y+1, y-1.
        """
        r_x, r_y = x, y
        if (r_x, r_y) not in self._routers:
            r_x = x + 1

            if x == self.dim_x - 1 or (r_x, r_y) not in self._routers:
                r_x = x - 1

                if x == 0 or (r_x, r_y) not in self._routers:
                    r_x = x
                    r_y = y + 1

                    if y >= self.dim_y - 1 or (r_x, r_y) not in self._routers:
                        r_y = y - 1
                        if y == 0:
                            r_x = x
                            r_y = y

        if (r_x, r_y) not in self._routers:
            raise RuntimeError(f'No router found for network interface (position: ({x}, {y}))')

        return r_x, r_y

    @staticmethod
    def _dir_from(router_x: int, router_y: int, from_x: int, from_y: int) -> int:
        """Direction of a router port serving position (from_x, from_y)."""
        if from_x != router_x:
            return DIR_LEFT if from_x < router_x else DIR_RIGHT
        elif from_y != router_y:
            return DIR_DOWN if from_y < router_y else DIR_UP
        else:
            return DIR_LOCAL

    def _router_in_itf(self, pos, nw: int, dir: int) -> gvsoc.systree.SlaveItf:
        """Bindable input of a position's router — the tile port when tiled."""
        tile = self._router_tiles.get(pos)
        if tile is not None:
            return gvsoc.systree.SlaveItf(tile,
                f'{_NW_NAMES[nw]}_input_{_DIR_NAMES[dir]}', signature='floonoc_link')
        return self._routers[pos][nw].i_INPUT(dir)

    def _router_out_bind(self, pos, nw: int, dir: int, itf: gvsoc.systree.SlaveItf):
        """Bind a position's router output — through the tile port when tiled."""
        tile = self._router_tiles.get(pos)
        if tile is not None:
            tile.itf_bind(f'{_NW_NAMES[nw]}_output_{_DIR_NAMES[dir]}', itf,
                signature='floonoc_link')
        else:
            self._routers[pos][nw].o_OUTPUT(dir, itf)

    def finalize(self):
        # Every NI holds the full memory map so it can translate addresses to
        # mesh positions on its own. Distributed here so callers can keep
        # adding mappings after construction.
        for ni, _tile in self._nis.values():
            ni.add_property('mappings', self.mappings)

        # Router meshes: bind each router's directional outputs to the
        # matching input of the neighbouring router of the same network.
        # Directions with no router are left for the NI attachment below
        # (border NIs) or stay unbound (mesh edges).
        for (x, y), routers in self._routers.items():
            neighbours = {DIR_RIGHT: (x+1, y), DIR_LEFT: (x-1, y), DIR_UP: (x, y+1), DIR_DOWN: (x, y-1)}
            for dir, pos in neighbours.items():
                if pos in self._routers:
                    for nw in range(len(_NW_NAMES)):
                        self._router_out_bind((x, y), nw, dir,
                            self._router_in_itf(pos, nw, _DIR_OPPOSITE[dir]))

        # NI attachment: each NI injects into and receives from the routers of
        # the three networks at its attachment position, through the router
        # ports facing the NI's own position (LOCAL for same-tile NIs, the
        # facing directional ports for border NIs).
        for (x, y), (ni, ni_tile) in self._nis.items():
            r_pos = self._ni_router_pos(x, y)
            dir = self._dir_from(*r_pos, x, y)

            if ni_tile is not None and ni_tile is self._router_tiles.get(r_pos):
                # NI and routers grouped in the same tile: plain sibling
                # bindings inside the tile.
                for nw in range(len(_NW_NAMES)):
                    router = self._routers[r_pos][nw]
                    ni.o_LINK(nw, router.i_INPUT(dir))
                    router.o_OUTPUT(dir, ni.i_LINK(nw))
            else:
                # Flat NI, or border NI in its own tile: bind through the
                # tile-level pass-throughs of whichever side is tiled.
                for nw in range(len(_NW_NAMES)):
                    in_itf = self._router_in_itf(r_pos, nw, dir)
                    if ni_tile is not None:
                        ni_tile.itf_bind(f'{_NW_NAMES[nw]}_ni_link_out', in_itf,
                            signature='floonoc_link')
                        ni_in_itf = gvsoc.systree.SlaveItf(ni_tile,
                            f'{_NW_NAMES[nw]}_ni_link_in', signature='floonoc_link')
                    else:
                        ni.o_LINK(nw, in_itf)
                        ni_in_itf = ni.i_LINK(nw)
                    self._router_out_bind(r_pos, nw, dir, ni_in_itf)


class FlooNocV22dMeshNarrowWide(gvsoc.systree.Component):
    """FlooNoC v2 (io v2 protocol) instance for a 2D mesh.

    Mirrors the v1 FlooNoc2dMeshNarrowWide generator API: a composite wrapping
    a FlooNocV2MeshFabric, exposing per-position external ports and the
    address-map API. add_router() and add_network_interface() instantiate real
    router/NI components (optionally grouped into per-position tiles), and
    finalize() binds the mesh together with 'floonoc_link' ports. External
    ports speak the v2 io protocol (vp/itf/io_v2.hpp) — burst beats with
    is_first/is_last/burst_id and the retry() deny handshake.
    """
    def __init__(self, parent: gvsoc.systree.Component, name, narrow_width: int, wide_width:int,
            dim_x: int, dim_y:int, ni_outstanding_reqs: int=8, router_input_queue_size: int=2,
            max_burst_size: int=4096):
        super().__init__(parent, name)

        self.add_property('mappings', {})
        self.add_property('routers', [])
        self.add_property('network_interfaces', [])
        self.add_property('ni_outstanding_reqs', ni_outstanding_reqs)
        self.add_property('narrow_width', narrow_width)
        self.add_property('wide_width', wide_width)
        self.add_property('dim_x', dim_x)
        self.add_property('dim_y', dim_y)
        self.add_property('router_input_queue_size', router_input_queue_size)

        # The fabric shares the 'mappings' property dict so both o_MAP-style
        # calls and direct property pokes keep feeding the NIs.
        self._fabric = FlooNocV2MeshFabric(self, narrow_width=narrow_width,
            wide_width=wide_width, dim_x=dim_x, dim_y=dim_y,
            ni_outstanding_reqs=ni_outstanding_reqs,
            router_input_queue_size=router_input_queue_size,
            mappings=self.get_property('mappings'), max_burst_size=max_burst_size)

    def __add_mapping(self, name: str, base: int, size: int, x: int, y: int, remove_offset:int =0):
        self._fabric.add_mapping(name, base=base, size=size, x=x, y=y, remove_offset=remove_offset)

    def add_router(self, x: int, y: int, tile: str=None):
        self.get_property('routers').append([x, y])
        self._fabric.add_router(x, y, tile=tile)

    def add_network_interface(self, x: int, y: int, tile: str=None):
        self.get_property('network_interfaces').append([x, y])
        self._fabric.add_network_interface(x, y, tile=tile)

        # External port pass-throughs at noc level. These must be registered
        # eagerly (not in finalize) so the composite-level virtual ports are
        # part of the generated ports list.
        narrow_width = self.get_property('narrow_width')
        wide_width = self.get_property('wide_width')
        self.itf_bind(f'narrow_input_{x}_{y}', self._fabric.i_NARROW_INPUT(x, y),
            signature=IoV2Beat(narrow_width), composite_bind=True)
        self.itf_bind(f'wide_input_{x}_{y}', self._fabric.i_WIDE_INPUT(x, y),
            signature=IoV2Beat(wide_width), composite_bind=True)
        self._fabric.o_NARROW_BIND(gvsoc.systree.SlaveItf(self, f'ni_narrow_{x}_{y}',
            signature=IoV2Beat(narrow_width)), x, y)
        self._fabric.o_WIDE_BIND(gvsoc.systree.SlaveItf(self, f'ni_wide_{x}_{y}',
            signature=IoV2Beat(wide_width)), x, y)

    def o_NARROW_MAP(self, itf: gvsoc.systree.SlaveItf, base: int, size: int,
            x: int, y: int, name: str=None, rm_base: bool=False, remove_offset:int =0):
        if name is None:
            name = itf.component.name
        if rm_base and remove_offset == 0:
            remove_offset = base
        self.__add_mapping(f"narrow_{name}", base=base, size=size, x=x, y=y, remove_offset=remove_offset)
        self.itf_bind(f"ni_narrow_{x}_{y}", itf, signature=IoV2Beat(self.get_property('narrow_width')))

    def o_WIDE_BIND(self, itf: gvsoc.systree.SlaveItf, x: int, y: int):
        self.itf_bind(f"ni_wide_{x}_{y}", itf, signature=IoV2Beat(self.get_property('wide_width')))

    def o_NARROW_BIND(self, itf: gvsoc.systree.SlaveItf, x: int, y: int):
        self.itf_bind(f"ni_narrow_{x}_{y}", itf, signature=IoV2Beat(self.get_property('narrow_width')))

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
        return gvsoc.systree.SlaveItf(self, f'narrow_input_{x}_{y}',
            signature=IoV2Beat(self.get_property('narrow_width')))

    def i_WIDE_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'wide_input_{x}_{y}',
            signature=IoV2Beat(self.get_property('wide_width')))

    def finalize(self):
        self._fabric.finalize()


class FlooNocV2ClusterGridNarrowWide(FlooNocV22dMeshNarrowWide):
    """FlooNoC v2 instance for a grid of clusters (mirrors v1's variant).

    With tiles=True, the routers and NI of each position are grouped into a
    per-position FloonocTileV2 composite (tile_x_y) for GUI/trace clarity.
    """
    def __init__(self, parent: gvsoc.systree.Component, name, wide_width: int, narrow_width:int, nb_x_clusters: int,
            nb_y_clusters, router_input_queue_size=2, ni_outstanding_reqs: int=2,
            tiles: bool=False, max_burst_size: int=4096):
        super().__init__(parent, name, wide_width=wide_width, narrow_width=narrow_width,
            dim_x=nb_x_clusters+2, dim_y=nb_y_clusters+2,
            router_input_queue_size=router_input_queue_size,
            ni_outstanding_reqs=ni_outstanding_reqs, max_burst_size=max_burst_size)

        def tile_name(x, y):
            return f'tile_{x}_{y}' if tiles else None

        for tile_x in range(0, nb_x_clusters):
            for tile_y in range(0, nb_y_clusters):
                self.add_router(tile_x+1, tile_y+1, tile=tile_name(tile_x+1, tile_y+1))
        for tile_x in range(0, nb_x_clusters+2):
            for tile_y in range(0, nb_y_clusters+2):
                if not ((tile_x == 0 and tile_y == 0) or (tile_x == 0 and tile_y == nb_y_clusters+1) \
                        or (tile_x == nb_x_clusters+1 and tile_y == 0) or \
                        (tile_x == nb_x_clusters+1 and tile_y == nb_y_clusters+1)):
                    self.add_network_interface(tile_x, tile_y, tile=tile_name(tile_x, tile_y))

    def i_CLUSTER_NARROW_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        return self.i_NARROW_INPUT(x+1, y+1)

    def i_CLUSTER_WIDE_INPUT(self, x: int, y: int) -> gvsoc.systree.SlaveItf:
        return self.i_WIDE_INPUT(x+1, y+1)
