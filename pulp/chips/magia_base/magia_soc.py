import gvsoc.systree
import memory.memory
import vp.clock_domain
import utils.loader.loader
import interco.router as router

from pulp.chips.magia_base.magia_tile import MagiaTile
from pulp.chips.magia_base.magia_arch import MagiaArch
from pulp.floonoc.floonoc import *
from typing import List

class MagiaSoc(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, binary):
        super().__init__(parent, name)

        loader=utils.loader.loader.ElfLoader(self, f'loader', binary=binary)

        # Single clock domain
        clock = vp.clock_domain.Clock_domain(self, 'tile-clock',
                                             frequency=MagiaArch.TILE_CLK_FREQ)
        clock.o_CLOCK(self.i_CLOCK())

        # L2 base model for testing
        l2_mem = memory.memory.Memory(self, 'test-mem', size=MagiaArch.L2_SIZE)

        # Create NoC
        #noc = FlooNocClusterGrid(self,
        #                         name='noc',
        #                         wide_width=64, narrow_width=4,
        #                         nb_x_clusters=MagiaArch.N_TILES_X, nb_y_clusters=MagiaArch.N_TILES_Y)

        soc_xbar = router.Router(self, f'soc-xbar')

        # Create Tiles
        cluster:List[MagiaTile] = []
        for id in range(0,MagiaArch.NB_CLUSTERS):
            cluster.append(MagiaTile(self, f'magia-tile-{id}', parser, id))

        # # Bind tiles to noc
        # for id in range(0,MagiaArch.NB_CLUSTERS):
        #     tile_x = int(id / MagiaArch.N_TILES_X)
        #     tile_y = int(id % MagiaArch.N_TILES_Y)
        #     noc.o_NARROW_MAP(l2_mem.i_INPUT(),base=0,size=MagiaArch.L2_SIZE,x=tile_x+1,y=tile_y+1,rm_base=True)
        #     cluster[id].o_XBAR(noc.i_CLUSTER_NARROW_INPUT(tile_x,tile_y))

        for id in range(0,MagiaArch.NB_CLUSTERS):
            cluster[id].o_XBAR(soc_xbar.i_INPUT())
            soc_xbar.o_MAP(l2_mem.i_INPUT(),"l2_mem",base=0,size=MagiaArch.L2_SIZE,rm_base=True)

        # Bind loader
        for id in range(0,MagiaArch.NB_CLUSTERS):
            if id == 0:
                loader.o_OUT(cluster[id].i_LOADER()) #only first cluster loads the elf
            loader.o_START(cluster[id].i_FETCHEN())
            loader.o_ENTRY(cluster[id].i_ENTRY())


        # Single tile
        #t0 = MagiaTile(self, 'magia-tile-0', parser, tid=0)
        #t1 = MagiaTile(self, 'magia-tile-1', parser, tid=1)

        # Bindings
        #t0.o_XBAR(l2_mem.i_INPUT())
        #t1.o_XBAR(l2_mem.i_INPUT())

        # loader_t0.o_OUT(t0.i_LOADER())
        # loader_t0.o_START(t0.i_FETCHEN())
        # loader_t0.o_ENTRY(t0.i_ENTRY())

        #loader_t1.o_OUT(t1.i_LOADER())
        #loader_t1.o_START(t1.i_FETCHEN())
        #loader_t1.o_ENTRY(t1.i_ENTRY())