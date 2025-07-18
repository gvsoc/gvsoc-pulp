import gvsoc.systree
import memory.memory
import vp.clock_domain
import utils.loader.loader
import interco.router as router

from pulp.chips.magia_base.magia_tile import MagiaTile
from pulp.chips.magia_base.magia_arch import MagiaArch
from pulp.floonoc.floonoc import *
from pulp.fractal_sync.fractal_sync import *
from typing import List, Dict
import math

def n_fract_per_lvl(param: int) -> list[int]:
        max_power = int(math.log2(param))
        return [2**i for i in reversed(range(max_power))]

def calculate_north_south(n, tiling):
    """
    Calculates the north and south position of the fractals based on tiling parameter.
    """
    if n < tiling:
        north = n
    else:
        row = n // tiling
        column = n % tiling
        north = row * 2 * tiling + column
    
    south = north + tiling
    return north, south

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

        # Create Tile matrix for IDs
        # --------------> X direction
        # | 0  1  2  3
        # | 4  5  6  7
        # | 8  9 10 11
        # |12 13 14 15
        # |
        # V
        # Y direction

        # Init matrix:
        tile_matrix: List[List[int]] = [[0 for _ in range(MagiaArch.N_TILES_X)] for _ in range(MagiaArch.N_TILES_Y)]
        # Populate matrix
        id=0
        for y in range(0,MagiaArch.N_TILES_Y):
            for x in range(0,MagiaArch.N_TILES_X):
                tile_matrix[y][x] = id
                id = id +1

        for row in tile_matrix:
            print(row)

        # Create fractal sync
        fsync_nord:List[FractalSync] = [] #used only at level 0
        fsync_sud:List[FractalSync] = [] #used only at level 0
        #fsync_center:List[List[FractalSync]] = []
        fsync_center: Dict[int, List[FractalSync]] = {}

        # fsync_east:List[List[FractalSync]] = []
        # fsync_west:List[List[FractalSync]] = []

        # Build horizontal tree. This tree builds also the root node
        lvl=0
        for n_fractal in n_fract_per_lvl(MagiaArch.NB_CLUSTERS):
            print(f"Placing {n_fractal} fsync in level {lvl}")
            if lvl == 0:
                for n in range(0,int(n_fractal/2)):
                    fsync_nord.append(FractalSync(self,f'fsync_nord_id_{n}',level=lvl))
                    fsync_sud.append(FractalSync(self,f'fsync_sud_id_{n}',level=lvl))
            else:
                if n_fractal == 1:
                    fsync_root = FractalSync(self,f'fsync_root',level=lvl)
                else :
                    fsync_center[lvl] = [None] * int(n_fractal)
                    for n in range(0,n_fractal):
                        fsync_center[lvl][n] = FractalSync(self,f'fsync_center_lvl_{lvl}_id_{n}',level=lvl)
            lvl=lvl+1

        # # Build vertical tree. 
        # lvl=0
        # for n_fractal in n_fract_per_lvl(MagiaArch.NB_CLUSTERS):
        #     # allocate a list per level
        #     fsync_east.append([None] * int(n_fractal / 2))
        #     fsync_west.append([None] * int(n_fractal / 2))

        #     for n in range(0,int(n_fractal/2)):
        #         fsync_east[lvl][n] = FractalSync(self,f'fsync_east_g{n}_lvl{lvl}',level=lvl)
        #         fsync_west[lvl][n] = FractalSync(self,f'fsync_west_g{n}_lvl{lvl}',level=lvl)
        #     lvl=lvl+1        

        if (MagiaArch.ENABLE_NOC):
            # this is WIP!!
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

            # Horizontal tree routing
            for lvl in range(0,int(math.log2(MagiaArch.NB_CLUSTERS))):
                # level 0 is a special level connecting the tiles
                if lvl == 0:
                    print("Current level is ", lvl)
                    # get the list of tiles connected to fractal nord west --> even rows and even cols of tile_matrix
                    tiles_even_rows_cols = [item for row in tile_matrix[::2] for item in row[::2]]
                    n=0
                    for id in tiles_even_rows_cols:
                        print(f"Connection tile-id {id} to fsync_nord_id_{n} WEST INPUT port")
                        cluster[id].o_SLAVE_EAST_WEST_FRACTAL(fsync_nord[n].i_SLAVE_WEST())
                        fsync_nord[n].o_SLAVE_WEST(cluster[id].i_SLAVE_EAST_WEST_FRACTAL())
                        n=n+1

                    # get the list of tiles connected to fractal sud west --> even rows and odd cols of tile_matrix
                    tiles_even_rows_odd_cols = [item for row in tile_matrix[::2] for item in row[1::2]]
                    n=0
                    for id in tiles_even_rows_odd_cols:
                        print(f"Connection tile-id {id} to fsync_nord_id_{n} EAST INPUT port")
                        cluster[id].o_SLAVE_EAST_WEST_FRACTAL(fsync_nord[n].i_SLAVE_EAST())
                        fsync_nord[n].o_SLAVE_EAST(cluster[id].i_SLAVE_EAST_WEST_FRACTAL())
                        n=n+1

                    # get the list of tiles connected to fractal nord east --> odd rows and even cols of tile_matrix
                    tiles_odd_rows_even_cols = [item for row in tile_matrix[1::2] for item in row[::2]]
                    n=0
                    for id in tiles_odd_rows_even_cols:
                        print(f"Connection tile-id {id} to fsync_sud_id_{n} WEST INPUT port")
                        cluster[id].o_SLAVE_EAST_WEST_FRACTAL(fsync_sud[n].i_SLAVE_WEST())
                        fsync_sud[n].o_SLAVE_WEST(cluster[id].i_SLAVE_EAST_WEST_FRACTAL())
                        n=n+1
                        
                    # get the list of tiles connected to fractal sud east --> odd rows and odd cols of tile_matrix
                    tiles_odd_rows_cols = [item for row in tile_matrix[1::2] for item in row[1::2]]
                    n=0
                    for id in tiles_odd_rows_cols:
                        print(f"Connection tile-id {id} to fsync_sud_id_{n} EAST INPUT port")
                        cluster[id].o_SLAVE_EAST_WEST_FRACTAL(fsync_sud[n].i_SLAVE_EAST())
                        fsync_sud[n].o_SLAVE_EAST(cluster[id].i_SLAVE_EAST_WEST_FRACTAL())
                        n=n+1
      
                elif (lvl == 1) and (lvl<(int(math.log2(MagiaArch.NB_CLUSTERS))-1)): #this is another special level as from now on we leave the nord-sud naming and we move to a more abstract form
                    print("Current level is ", lvl)
                    for n in range(0,len(fsync_center[lvl])):
                        print(f"Connecting fsync_nord_id_{n} NORD_SUD OUTPUT port to fsync_center_lvl_{lvl}_id_{n} NORD INPUT port")
                        fsync_nord[n].o_MASTER_NORD_SUD(fsync_center[lvl][n].i_SLAVE_NORD())
                        fsync_center[lvl][n].o_SLAVE_NORD(fsync_nord[n].i_MASTER_NORD_SUD())

                        print(f"Connecting fsync_sud_id_{n} NORD_SUD OUTPUT port to fsync_center_lvl_{lvl}_id_{n} SUD INPUT port")
                        fsync_sud[n].o_MASTER_NORD_SUD(fsync_center[lvl][n].i_SLAVE_SUD())
                        fsync_center[lvl][n].o_SLAVE_SUD(fsync_sud[n].i_MASTER_NORD_SUD())
                
                elif (lvl > 1) and (lvl<(int(math.log2(MagiaArch.NB_CLUSTERS))-1)): # intermediate levels
                    print("Current level is ", lvl)
                    if lvl % 2 == 0: #fractal in even levels use EAST WEST ports
                        n_prev=0
                        for n in range(0,len(fsync_center[lvl])):
                            print(f"Connecting fsync_center_lvl_{lvl-1}_id_{n_prev} EAST_WEST OUTPUT port to fsync_center_lvl_{lvl}_id_{n} WEST INPUT port")
                            fsync_center[lvl-1][n_prev].o_MASTER_EAST_WEST(fsync_center[lvl][n].i_SLAVE_WEST())
                            fsync_center[lvl][n].o_SLAVE_WEST(fsync_center[lvl-1][n_prev].i_MASTER_EAST_WEST())
                            n_prev=n_prev+1
                            print(f"Connecting fsync_center_lvl_{lvl-1}_id_{n_prev} EAST_WEST OUTPUT port to fsync_center_lvl_{lvl}_id_{n} EAST INPUT port")
                            fsync_center[lvl-1][n_prev].o_MASTER_EAST_WEST(fsync_center[lvl][n].i_SLAVE_EAST())
                            fsync_center[lvl][n].o_SLAVE_EAST(fsync_center[lvl-1][n_prev].i_MASTER_EAST_WEST())
                            n_prev=n_prev+1

                    else : #fractal in odd levels use NORD SUD ports
                        n_prev=0
                        for n in range(0,len(fsync_center[lvl])):
                            nord_id,sud_id=calculate_north_south(n,math.isqrt(len(fsync_center[lvl])))
                            print(f"Connecting fsync_center_lvl_{lvl-1}_id_{nord_id} NORD_SUD OUTPUT port to fsync_center_lvl_{lvl}_id_{n} NORD INPUT port")
                            fsync_center[lvl-1][nord_id].o_MASTER_NORD_SUD(fsync_center[lvl][n].i_SLAVE_NORD())
                            fsync_center[lvl][n].o_SLAVE_NORD(fsync_center[lvl-1][nord_id].i_MASTER_NORD_SUD())
                    
                            print(f"Connecting fsync_center_lvl_{lvl-1}_id_{sud_id} NORD_SUD OUTPUT port to fsync_center_lvl_{lvl}_id_{n} SUD INPUT port")
                            fsync_center[lvl-1][sud_id].o_MASTER_NORD_SUD(fsync_center[lvl][n].i_SLAVE_SUD())
                            fsync_center[lvl][n].o_SLAVE_SUD(fsync_center[lvl-1][sud_id].i_MASTER_NORD_SUD())
                        
                
                else: #this is the root
                    print("Current level is ", lvl, ". Connecting root node.")
                    if lvl == 1: # this is a special case
                        print(f"Connecting fsync_nord_id_{0} NORD_SUD OUTPUT port to fsync_root NORD INPUT port")
                        fsync_nord[0].o_MASTER_NORD_SUD(fsync_root.i_SLAVE_NORD())
                        fsync_root.o_SLAVE_NORD(fsync_nord[0].i_MASTER_NORD_SUD())

                        print(f"Connecting fsync_sud_id_{0} NORD_SUD OUTPUT port to fsync_root SUD INPUT port")
                        fsync_sud[0].o_MASTER_NORD_SUD(fsync_root.i_SLAVE_SUD())
                        fsync_root.o_SLAVE_SUD(fsync_sud[0].i_MASTER_NORD_SUD())

                    else :  
                        #please note that last level, i.e., root level, is always even. Moreover the previous level has only one fractal NORD and one fractal SUD
                        print(f"Connecting fsync_center_lvl_{lvl-1}_id_{0} NORD_SUD OUTPUT port to fsync_root NORD INPUT port")
                        fsync_center[lvl-1][0].o_MASTER_NORD_SUD(fsync_root.i_SLAVE_NORD())
                        fsync_root.o_SLAVE_NORD(fsync_center[lvl-1][0].i_MASTER_NORD_SUD())

                        print(f"Connecting fsync_center_lvl_{lvl-1}_id_{1} NORD_SUD OUTPUT port to fsync_root SUD INPUT port")
                        fsync_center[lvl-1][1].o_MASTER_NORD_SUD(fsync_root.i_SLAVE_SUD())
                        fsync_root.o_SLAVE_SUD(fsync_center[lvl-1][1].i_MASTER_NORD_SUD())

        # Bind loader
        for id in range(0,MagiaArch.NB_CLUSTERS):
            if (id == 0):
                loader.o_OUT(cluster[id].i_LOADER()) #only cluster connected to the corner loads the elf
            loader.o_START(cluster[id].i_FETCHEN())
            loader.o_ENTRY(cluster[id].i_ENTRY())