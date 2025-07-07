import gvsoc.systree
import memory.memory
import vp.clock_domain
import utils.loader.loader
import interco.router as router

from pulp.chips.magia_base.magia_tile import MagiaTile
from pulp.chips.magia_base.magia_arch import MagiaArch
from pulp.floonoc.floonoc import *
from pulp.fractal_sync.fractal_sync import *
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

        # Create Tiles
        cluster:List[MagiaTile] = []
        for id in range(0,MagiaArch.NB_CLUSTERS):
            cluster.append(MagiaTile(self, f'magia-tile-{id}', parser, id))

        # Create fractal sync
        fsync_nord = FractalSync(self,'fsync_nord',level=0)
        fsync_sud  = FractalSync(self,'fsync_sud',level=0)
        fsync_east = FractalSync(self,'fsync_east',level=0)  
        fsync_west = FractalSync(self,'fsync_west',level=0)
          
        fsync_center = FractalSync(self,'fsync_center',level=1)

        if (MagiaArch.ENABLE_NOC):
            noc = FlooNoc2dMeshNarrowWide(self,
                                        name='magia-noc',
                                        narrow_width=4,
                                        wide_width=64,
                                        dim_x=MagiaArch.N_TILES_X+1, dim_y=MagiaArch.N_TILES_Y)
            

            # Create noc routers
            noc.add_router(0, 0) # Add a router at each target
            noc.add_router(1, 0) # Add a router at each cluster
            noc.add_router(2, 0) # Add a router at each cluster
            noc.add_router(1, 1) # Add a router at each cluster
            noc.add_router(2, 1) # Add a router at each cluster

            # Create NI
            noc.add_network_interface(0, 0) #this is the NI used by the L2 memory
            noc.add_network_interface(1, 0)
            noc.add_network_interface(2, 0)
            #noc.add_network_interface(0, 1) 
            noc.add_network_interface(1, 1)
            noc.add_network_interface(2, 1)

            # Bind clusters to noc
            cluster[0].o_NARROW_OUTPUT(noc.i_NARROW_INPUT(1,0))
            cluster[1].o_NARROW_OUTPUT(noc.i_NARROW_INPUT(2,0))
            cluster[2].o_NARROW_OUTPUT(noc.i_NARROW_INPUT(1,1))
            cluster[3].o_NARROW_OUTPUT(noc.i_NARROW_INPUT(2,1))

            # Bind noc to clusters
            #noc.o_NARROW_MAP(cluster[0].i_NARROW_INPUT(),base=MagiaArch.L1_ADDR_START+MagiaArch.L1_TILE_OFFSET*0,size=MagiaArch.L1_SIZE,x=1,y=0,rm_base=False)
            #noc.o_NARROW_MAP(cluster[1].i_NARROW_INPUT(),base=MagiaArch.L1_ADDR_START+MagiaArch.L1_TILE_OFFSET*1,size=MagiaArch.L1_SIZE,x=2,y=0,rm_base=False)
            #noc.o_NARROW_MAP(cluster[2].i_NARROW_INPUT(),base=MagiaArch.L1_ADDR_START+MagiaArch.L1_TILE_OFFSET*2,size=MagiaArch.L1_SIZE,x=1,y=1,rm_base=False)
            #noc.o_NARROW_MAP(cluster[3].i_NARROW_INPUT(),base=MagiaArch.L1_ADDR_START+MagiaArch.L1_TILE_OFFSET*3,size=MagiaArch.L1_SIZE,x=2,y=1,rm_base=False)

            # Bind memory to noc
            #noc.o_NARROW_MAP(l2_mem.i_INPUT(),base=0,size=MagiaArch.L2_SIZE,x=0,y=0,rm_base=True)
            noc.o_NARROW_MAP(l2_mem.i_INPUT(),name='l2map',base=0,size=MagiaArch.L2_SIZE,x=0,y=0,rm_base=False)

        else:

            soc_xbar = router.Router(self, f'soc-xbar')
            
            for id in range(0,MagiaArch.NB_CLUSTERS):
                cluster[id].o_NARROW_OUTPUT(soc_xbar.i_INPUT())
                soc_xbar.o_MAP(cluster[id].i_NARROW_INPUT(),f'tile-{id}-l1-mem',base=MagiaArch.L1_ADDR_START+(id*MagiaArch.L1_TILE_OFFSET),size=MagiaArch.L1_SIZE,rm_base=False)

            soc_xbar.o_MAP(l2_mem.i_INPUT(),"l2_mem",base=MagiaArch.L2_ADDR_START,size=MagiaArch.L2_SIZE,rm_base=True)

            cluster[0].o_SLAVE_EAST_WEST_FRACTAL(fsync_nord.i_SLAVE_WEST())
            fsync_nord.o_SLAVE_WEST(cluster[0].i_SLAVE_EAST_WEST_FRACTAL())

            cluster[1].o_SLAVE_EAST_WEST_FRACTAL(fsync_nord.i_SLAVE_EAST())
            fsync_nord.o_SLAVE_EAST(cluster[1].i_SLAVE_EAST_WEST_FRACTAL())

            cluster[0].o_SLAVE_NORD_SUD_FRACTAL(fsync_west.i_SLAVE_NORD())
            fsync_west.o_SLAVE_NORD(cluster[0].i_SLAVE_NORD_SUD_FRACTAL())

            cluster[1].o_SLAVE_NORD_SUD_FRACTAL(fsync_east.i_SLAVE_NORD())
            fsync_east.o_SLAVE_NORD(cluster[1].i_SLAVE_NORD_SUD_FRACTAL())
   
            cluster[2].o_SLAVE_NORD_SUD_FRACTAL(fsync_west.i_SLAVE_SUD())
            fsync_west.o_SLAVE_SUD(cluster[2].i_SLAVE_NORD_SUD_FRACTAL())

            cluster[3].o_SLAVE_NORD_SUD_FRACTAL(fsync_east.i_SLAVE_SUD())
            fsync_east.o_SLAVE_SUD(cluster[3].i_SLAVE_NORD_SUD_FRACTAL())

            cluster[2].o_SLAVE_EAST_WEST_FRACTAL(fsync_sud.i_SLAVE_WEST())
            fsync_sud.o_SLAVE_WEST(cluster[2].i_SLAVE_EAST_WEST_FRACTAL())

            cluster[3].o_SLAVE_EAST_WEST_FRACTAL(fsync_sud.i_SLAVE_EAST())
            fsync_sud.o_SLAVE_EAST(cluster[3].i_SLAVE_EAST_WEST_FRACTAL())

            fsync_nord.o_MASTER_NORD_SUD(fsync_center.i_SLAVE_NORD())
            fsync_center.o_SLAVE_NORD(fsync_nord.i_MASTER_NORD_SUD())

            fsync_west.o_MASTER_EAST_WEST(fsync_center.i_SLAVE_WEST())
            fsync_center.o_SLAVE_WEST(fsync_west.i_MASTER_EAST_WEST())

            fsync_sud.o_MASTER_NORD_SUD(fsync_center.i_SLAVE_SUD())
            fsync_center.o_SLAVE_SUD(fsync_sud.i_MASTER_NORD_SUD())

            fsync_east.o_MASTER_EAST_WEST(fsync_center.i_SLAVE_EAST())
            fsync_center.o_SLAVE_EAST(fsync_east.i_MASTER_EAST_WEST())

        # Bind loader
        for id in range(0,MagiaArch.NB_CLUSTERS):
            if (id == 0):
                loader.o_OUT(cluster[id].i_LOADER()) #only cluster connected to the corner loads the elf
            loader.o_START(cluster[id].i_FETCHEN())
            loader.o_ENTRY(cluster[id].i_ENTRY())