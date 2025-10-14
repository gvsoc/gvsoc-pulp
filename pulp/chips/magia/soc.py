# Copyright (C) 2025 Fondazione Chips-IT

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree
import memory.memory as memory
import vp.clock_domain
import utils.loader.loader
import interco.router as router

from pulp.chips.magia.tile import MagiaTile
from pulp.chips.magia.arch import MagiaArch
from pulp.floonoc.floonoc import *
from pulp.chips.magia.fractal_sync import *
from pulp.chips.magia.kill_module import *
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

        # Bin Loader
        loader=utils.loader.loader.ElfLoader(self, f'loader', binary=binary)

        # Simulation engine killer
        killer=KillModule(self,'kill-module',kill_addr_base=MagiaArch.TEST_END_ADDR_START,kill_addr_size=MagiaArch.TEST_END_SIZE,nb_cores_to_wait=MagiaArch.NB_CLUSTERS)

        # Single clock domain
        clock = vp.clock_domain.Clock_domain(self, 'tile-clock',
                                             frequency=MagiaArch.TILE_CLK_FREQ)
        clock.o_CLOCK(self.i_CLOCK())

        # Create Tiles
        cluster:List[MagiaTile] = []
        for id in range(0,MagiaArch.NB_CLUSTERS):
            cluster.append(MagiaTile(self, f'magia-tile-{id}', parser, id))

        l2_mem = memory.Memory(self, f'L2-mem', size=MagiaArch.L2_SIZE,latency=1)

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
        fsync_west:List[FractalSync] = [] #used only at level 0
        fsync_east:List[FractalSync] = [] #used only at level 0
        fsync_neighbour_east_west:List[FractalSync] = [] #used only at level 0
        fsync_neighbour_nord_sud:List[FractalSync] = [] #used only at level 0
        fsync_center_hv: Dict[int, List[FractalSync]] = {} # center fsync used by both the h-tree and the v-tree
        fsync_center_v: Dict[int, List[FractalSync]] = {} # center fsync used by v-tree
        # Place horizontal-vertical fsyncs
        lvl=0
        for n_fractal in n_fract_per_lvl(MagiaArch.NB_CLUSTERS):
            if lvl == 0:
                print(f"Placing {n_fractal*2} fsync in h+v tree at level {lvl}")
                for n in range(0,int(n_fractal/2)):
                    fsync_nord.append(FractalSync(self,f'fsync_nord_id_{n}',level=lvl))
                    fsync_sud.append(FractalSync(self,f'fsync_sud_id_{n}',level=lvl))
                    fsync_west.append(FractalSync(self,f'fsync_west_id_{n}',level=lvl))
                    fsync_east.append(FractalSync(self,f'fsync_east_id_{n}',level=lvl))
                    
            else:
                if n_fractal == 1:
                    print(f"Placing {n_fractal} fsync in root level {lvl}")
                    fsync_root = FractalSync(self,f'fsync_root',level=lvl)
                else :
                    # note. Center fsync on odd levels host also the vertical tree while in even levels V-tree has its own fractals
                    print(f"Placing {n_fractal} fsync in h+v tree at level {lvl}")
                    fsync_center_hv[lvl] = [None] * int(n_fractal)
                    for n in range(0,n_fractal):
                        fsync_center_hv[lvl][n] = FractalSync(self,f'fsync_center_hv_lvl_{lvl}_id_{n}',level=lvl)
                    if lvl % 2 == 0:
                        print(f"Placing {n_fractal} fsync in v tree at level {lvl}")
                        fsync_center_v[lvl] = [None] * int(n_fractal)
                        for n in range(0,n_fractal):
                            fsync_center_v[lvl][n] = FractalSync(self,f'fsync_center_v_lvl_{lvl}_id_{n}',level=lvl)
            lvl=lvl+1  

        # Place neighbour fsyncs (here level is always 0) only for achitectures > 2x2
        n_fractal_neighbour=0
        if MagiaArch.NB_CLUSTERS >= 4:
            n_fractal_neighbour=(((MagiaArch.N_TILES_X)//2) - 1)*(MagiaArch.N_TILES_Y)
            print(f"Placing {n_fractal_neighbour*2} neighbour fsync at level 0")
            for n_fractal in range(0,n_fractal_neighbour):
                fsync_neighbour_east_west.append(FractalSync(self,f'fsync_east_west_nb_id_{n_fractal}',level=0))
                fsync_neighbour_nord_sud.append(FractalSync(self,f'fsync_nord_sud_nb_id_{n_fractal}',level=0))


        #Connect NoC to tiles and L2    
        noc = FlooNoc2dMeshNarrowWide(self,
                                    name='magia-noc',
                                    narrow_width=4,
                                    wide_width=4,
                                    ni_outstanding_reqs=8, #need to double check this with RTL
                                    router_input_queue_size=4, #need to double check this with RTL
                                    dim_x=MagiaArch.N_TILES_X+1, dim_y=MagiaArch.N_TILES_Y)
        

        # Create noc routers
        for y in range(0,MagiaArch.N_TILES_Y):
            for x in range(0,MagiaArch.N_TILES_X+1):
                print(f"[NoC] Adding router and NI at position x={x} y={y}")
                noc.add_router(x, y)
                noc.add_network_interface(x, y)

        # Bind clusters to noc. E.g. for 4x4
        # {1.0}----{2.0}----{3.0}----{4.0}
        #   | 0      |  1     |  2     |  3
        #   |        |        |        |
        # {1.1}----{2.1}----{3.1}----{4.1}
        #   | 4      |  5     |  6     |  7
        #   |        |        |        |
        # {1.2}----{2.2}----{3.2}----{4.2}
        #   | 8      |  9     |  10    |  11
        #   |        |        |        |
        # {1.3}----{2.3}----{3.3}----{4.3}
        #     12        13       14       15                        

        id = 0
        for y in range(0,MagiaArch.N_TILES_Y):
            for x in range(1,MagiaArch.N_TILES_X+1):
                print(f"[NoC] Adding cluster {id} at position x={x} y={y}")
                cluster[id].o_KILLER_OUTPUT(killer.i_INPUT())
                cluster[id].o_NARROW_OUTPUT(noc.i_NARROW_INPUT(x,y))
                noc.o_NARROW_MAP(cluster[id].i_NARROW_INPUT(),name=f'tile-{id}-l1-mem',base=MagiaArch.L1_ADDR_START+(id*MagiaArch.L1_TILE_OFFSET),size=MagiaArch.L1_SIZE,x=x,y=y,rm_base=False)
                id += 1

        # Bind memory to noc
        # {0.0}----{1.0}----{2.0}----{3.0}----{4.0}
        #   | L2     | 0      |  1     |  2     |  3
        #   |        |        |        |        |
        # {0.1}----{1.1}----{2.1}----{3.1}----{4.1}
        #   | L2     | 4      |  5     |  6     |  7
        #   |        |        |        |        |
        # {0.2}----{1.2}----{2.2}----{3.2}----{4.2}
        #   | L2     | 8      |  9     |  10    |  11
        #   |        |        |        |        |
        # {0.3}----{1.3}----{2.3}----{3.3}----{4.3}
        #     L2       12        13       14       15

        for y in range(0,MagiaArch.N_TILES_Y):
            print(f"[NoC] Adding L2 at position x={0} y={y}")
            noc.o_NARROW_MAP(l2_mem.i_INPUT(),name=f'l2-map-{y}',base=MagiaArch.L2_ADDR_START,size=MagiaArch.L2_SIZE,x=0,y=y,rm_base=True)

        # Fractal tree routing
        for lvl in range(0,int(math.log2(MagiaArch.NB_CLUSTERS))):
            # level 0 is a special level connecting the tiles
            if lvl == 0:
                print("Current level is ", lvl)
                # get the list of tiles connected to fractal nord west --> even rows and even cols of tile_matrix
                tiles_even_rows_even_cols = [item for row in tile_matrix[::2] for item in row[::2]]
                n=0
                for id in tiles_even_rows_even_cols:
                    #print(f"Connection tile-id {id} to fsync_nord_id_{n} WEST INPUT port")
                    cluster[id].o_SLAVE_EAST_WEST_FRACTAL(fsync_nord[n].i_SLAVE_WEST())
                    fsync_nord[n].o_SLAVE_WEST(cluster[id].i_SLAVE_EAST_WEST_FRACTAL())
                    #print(f"Connection tile-id {id} to fsync_west_id_{n} NORD INPUT port")
                    cluster[id].o_SLAVE_NORD_SUD_FRACTAL(fsync_west[n].i_SLAVE_NORD())
                    fsync_west[n].o_SLAVE_NORD(cluster[id].i_SLAVE_NORD_SUD_FRACTAL())
                    n=n+1

                # get the list of tiles connected to fractal sud west --> even rows and odd cols of tile_matrix
                tiles_even_rows_odd_cols = [item for row in tile_matrix[::2] for item in row[1::2]]
                n=0
                for id in tiles_even_rows_odd_cols:
                    #print(f"Connection tile-id {id} to fsync_nord_id_{n} EAST INPUT port")
                    cluster[id].o_SLAVE_EAST_WEST_FRACTAL(fsync_nord[n].i_SLAVE_EAST())
                    fsync_nord[n].o_SLAVE_EAST(cluster[id].i_SLAVE_EAST_WEST_FRACTAL())
                    #print(f"Connection tile-id {id} to fsync_east_id_{n} NORD INPUT port")
                    cluster[id].o_SLAVE_NORD_SUD_FRACTAL(fsync_east[n].i_SLAVE_NORD())
                    fsync_east[n].o_SLAVE_NORD(cluster[id].i_SLAVE_NORD_SUD_FRACTAL())
                    n=n+1

                # get the list of tiles connected to fractal nord east --> odd rows and even cols of tile_matrix
                tiles_odd_rows_even_cols = [item for row in tile_matrix[1::2] for item in row[::2]]
                n=0
                for id in tiles_odd_rows_even_cols:
                    #print(f"Connection tile-id {id} to fsync_sud_id_{n} WEST INPUT port")
                    cluster[id].o_SLAVE_EAST_WEST_FRACTAL(fsync_sud[n].i_SLAVE_WEST())
                    fsync_sud[n].o_SLAVE_WEST(cluster[id].i_SLAVE_EAST_WEST_FRACTAL())
                    #print(f"Connection tile-id {id} to fsync_west_id_{n} SUD INPUT port")
                    cluster[id].o_SLAVE_NORD_SUD_FRACTAL(fsync_west[n].i_SLAVE_SUD())
                    fsync_west[n].o_SLAVE_SUD(cluster[id].i_SLAVE_NORD_SUD_FRACTAL())
                    n=n+1
                    
                # get the list of tiles connected to fractal sud east --> odd rows and odd cols of tile_matrix
                tiles_odd_rows_odd_cols = [item for row in tile_matrix[1::2] for item in row[1::2]]
                n=0
                for id in tiles_odd_rows_odd_cols:
                    #print(f"Connection tile-id {id} to fsync_sud_id_{n} EAST INPUT port")
                    cluster[id].o_SLAVE_EAST_WEST_FRACTAL(fsync_sud[n].i_SLAVE_EAST())
                    fsync_sud[n].o_SLAVE_EAST(cluster[id].i_SLAVE_EAST_WEST_FRACTAL())
                    #print(f"Connection tile-id {id} to fsync_west_id_{n} SUD INPUT port")
                    cluster[id].o_SLAVE_NORD_SUD_FRACTAL(fsync_east[n].i_SLAVE_SUD())
                    fsync_east[n].o_SLAVE_SUD(cluster[id].i_SLAVE_NORD_SUD_FRACTAL())
                    n=n+1

                if n_fractal_neighbour > 0:
                    transposed = list(zip(*tile_matrix))
                    odd_columns= [transposed[i] for i in range(len(transposed) - 1) if i % 2 == 1]
                    n=0
                    for column in odd_columns:
                        for id in column:
                            id=int(id) #this is needed bacause of the zip function that gives a tuple rather than a list...
                            print(f"Connection tile-id {id} to fsync_neighbour_east_west_{n} WEST INPUT port")
                            cluster[id].o_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(fsync_neighbour_east_west[n].i_SLAVE_WEST())
                            fsync_neighbour_east_west[n].o_SLAVE_WEST(cluster[id].i_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL())
                            print(f"Connection tile-id {id+1} to fsync_neighbour_east_west_{n} EAST INPUT port")
                            cluster[id+1].o_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(fsync_neighbour_east_west[n].i_SLAVE_EAST())
                            fsync_neighbour_east_west[n].o_SLAVE_EAST(cluster[id+1].i_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL())
                            n=n+1
                    
                    odd_rows = [tile_matrix[i] for i in range(len(tile_matrix) - 1) if i % 2 == 1]
                    n=0
                    for row in odd_rows:
                        for id in row:
                            print(f"Connection tile-id {id} to fsync_neighbour_nord_sud_{n} NORD INPUT port")
                            cluster[id].o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(fsync_neighbour_nord_sud[n].i_SLAVE_NORD())
                            fsync_neighbour_nord_sud[n].o_SLAVE_NORD(cluster[id].i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL())
                            print(f"Connection tile-id {id+MagiaArch.N_TILES_X} to fsync_neighbour_nord_sud_{n} SUD INPUT port")
                            cluster[id+MagiaArch.N_TILES_X].o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(fsync_neighbour_nord_sud[n].i_SLAVE_SUD())
                            fsync_neighbour_nord_sud[n].o_SLAVE_SUD(cluster[id+MagiaArch.N_TILES_X].i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL())
                            n=n+1
    
            elif (lvl == 1) and (lvl<(int(math.log2(MagiaArch.NB_CLUSTERS))-1)): #this is another special level as from now on we leave the nord-sud naming and we move to a more abstract form
                print("Current level is ", lvl)
                # note. Center fsync on odd levels host also the vertical tree
                for n in range(0,len(fsync_center_hv[lvl])):
                    #print(f"Connecting fsync_nord_id_{n} NORD_SUD OUTPUT port to fsync_center_hv_lvl_{lvl}_id_{n} NORD INPUT port")
                    fsync_nord[n].o_MASTER_NORD_SUD(fsync_center_hv[lvl][n].i_SLAVE_NORD())
                    fsync_center_hv[lvl][n].o_SLAVE_NORD(fsync_nord[n].i_MASTER_NORD_SUD())

                    #print(f"Connecting fsync_sud_id_{n} NORD_SUD OUTPUT port to fsync_center_hv_lvl_{lvl}_id_{n} SUD INPUT port")
                    fsync_sud[n].o_MASTER_NORD_SUD(fsync_center_hv[lvl][n].i_SLAVE_SUD())
                    fsync_center_hv[lvl][n].o_SLAVE_SUD(fsync_sud[n].i_MASTER_NORD_SUD())

                    #print(f"Connecting fsync_west_id_{n} EAST_WEST OUTPUT port to fsync_center_hv_lvl_{lvl}_id_{n} WEST INPUT port")
                    fsync_west[n].o_MASTER_EAST_WEST(fsync_center_hv[lvl][n].i_SLAVE_WEST())
                    fsync_center_hv[lvl][n].o_SLAVE_WEST(fsync_west[n].i_MASTER_EAST_WEST())

                    #print(f"Connecting fsync_east_id_{n} EAST_WEST OUTPUT port to fsync_center_hv_lvl_{lvl}_id_{n} EAST INPUT port")
                    fsync_east[n].o_MASTER_EAST_WEST(fsync_center_hv[lvl][n].i_SLAVE_EAST())
                    fsync_center_hv[lvl][n].o_SLAVE_EAST(fsync_east[n].i_MASTER_EAST_WEST())
            
            elif (lvl > 1) and (lvl<(int(math.log2(MagiaArch.NB_CLUSTERS))-1)): # intermediate levels
                print("Current level is ", lvl)
                if lvl % 2 == 0: #fractal in even levels are not shared between H-tree and V-tree and use EAST WEST ports (H-tree) and NORD SUD ports (V-tree)
                    n_prev=0
                    for n in range(0,len(fsync_center_hv[lvl])):
                        #print(f"Connecting fsync_center_hv_lvl_{lvl-1}_id_{n_prev} EAST_WEST OUTPUT port to fsync_center_hv_lvl_{lvl}_id_{n} WEST INPUT port")
                        fsync_center_hv[lvl-1][n_prev].o_MASTER_EAST_WEST(fsync_center_hv[lvl][n].i_SLAVE_WEST())
                        fsync_center_hv[lvl][n].o_SLAVE_WEST(fsync_center_hv[lvl-1][n_prev].i_MASTER_EAST_WEST())
                        n_prev=n_prev+1
                        #print(f"Connecting fsync_center_hv_lvl_{lvl-1}_id_{n_prev} EAST_WEST OUTPUT port to fsync_center_hv_lvl_{lvl}_id_{n} EAST INPUT port")
                        fsync_center_hv[lvl-1][n_prev].o_MASTER_EAST_WEST(fsync_center_hv[lvl][n].i_SLAVE_EAST())
                        fsync_center_hv[lvl][n].o_SLAVE_EAST(fsync_center_hv[lvl-1][n_prev].i_MASTER_EAST_WEST())
                        n_prev=n_prev+1

                    for n in range(0,len(fsync_center_v[lvl])):
                        nord_id,sud_id=calculate_north_south(n,math.isqrt(len(fsync_center_hv[lvl-1])))
                        #print(f"Connecting fsync_center_hv_lvl_{lvl-1}_id_{nord_id} NORD SUD OUTPUT port to fsync_center_v_lvl_{lvl}_id_{n} NORD INPUT port")
                        fsync_center_hv[lvl-1][nord_id].o_MASTER_NORD_SUD(fsync_center_v[lvl][n].i_SLAVE_NORD())
                        fsync_center_v[lvl][n].o_SLAVE_NORD(fsync_center_hv[lvl-1][nord_id].i_MASTER_NORD_SUD())

                        #print(f"Connecting fsync_center_hv_lvl_{lvl-1}_id_{sud_id} NORD SUD OUTPUT port to fsync_center_v_lvl_{lvl}_id_{n} SUD INPUT port")
                        fsync_center_hv[lvl-1][sud_id].o_MASTER_NORD_SUD(fsync_center_v[lvl][n].i_SLAVE_SUD())
                        fsync_center_v[lvl][n].o_SLAVE_SUD(fsync_center_hv[lvl-1][sud_id].i_MASTER_NORD_SUD())

                else : #fractal in odd levels use NORD SUD ports
                    # note. Center fsync on odd levels host also the vertical tree
                    for n in range(0,len(fsync_center_hv[lvl])):
                        nord_id,sud_id=calculate_north_south(n,math.isqrt(len(fsync_center_hv[lvl])))
                        #print(f"Connecting fsync_center_lvl_{lvl-1}_id_{nord_id} NORD_SUD OUTPUT port to fsync_center_lvl_{lvl}_id_{n} NORD INPUT port")
                        fsync_center_hv[lvl-1][nord_id].o_MASTER_NORD_SUD(fsync_center_hv[lvl][n].i_SLAVE_NORD())
                        fsync_center_hv[lvl][n].o_SLAVE_NORD(fsync_center_hv[lvl-1][nord_id].i_MASTER_NORD_SUD())

                        #print(f"Connecting fsync_center_hv_lvl_{lvl-1}_id_{sud_id} NORD_SUD OUTPUT port to fsync_center_hv_lvl_{lvl}_id_{n} SUD INPUT port")
                        fsync_center_hv[lvl-1][sud_id].o_MASTER_NORD_SUD(fsync_center_hv[lvl][n].i_SLAVE_SUD())
                        fsync_center_hv[lvl][n].o_SLAVE_SUD(fsync_center_hv[lvl-1][sud_id].i_MASTER_NORD_SUD())
                    
                    n_prev=0
                    for n in range(0,len(fsync_center_hv[lvl])):
                        #print(f"Connecting fsync_center_v_lvl_{lvl-1}_id_{n_prev} EAST_WEST OUTPUT port to fsync_center_hv_lvl_{lvl}_id_{n} WEST INPUT port")
                        fsync_center_v[lvl-1][n_prev].o_MASTER_EAST_WEST(fsync_center_hv[lvl][n].i_SLAVE_WEST())
                        fsync_center_hv[lvl][n].o_SLAVE_WEST(fsync_center_v[lvl-1][n_prev].i_MASTER_EAST_WEST())
                        n_prev=n_prev+1
                        #print(f"Connecting fsync_center_v_lvl_{lvl-1}_id_{n_prev} EAST_WEST OUTPUT port to fsync_center_hv_lvl_{lvl}_id_{n} EAST INPUT port")
                        fsync_center_v[lvl-1][n_prev].o_MASTER_EAST_WEST(fsync_center_hv[lvl][n].i_SLAVE_EAST())
                        fsync_center_hv[lvl][n].o_SLAVE_EAST(fsync_center_v[lvl-1][n_prev].i_MASTER_EAST_WEST())
                        n_prev=n_prev+1
            
            else: #this is the root
                print("Current level is ", lvl, ". Connecting root node.")
                if lvl == 1: # this is a special case
                    #print(f"Connecting fsync_nord_id_{0} NORD_SUD OUTPUT port to fsync_root NORD INPUT port")
                    fsync_nord[0].o_MASTER_NORD_SUD(fsync_root.i_SLAVE_NORD())
                    fsync_root.o_SLAVE_NORD(fsync_nord[0].i_MASTER_NORD_SUD())

                    #print(f"Connecting fsync_west_id_{0} EAST_WEST OUTPUT port to fsync_root WEST INPUT port")
                    fsync_west[0].o_MASTER_EAST_WEST(fsync_root.i_SLAVE_WEST())
                    fsync_root.o_SLAVE_WEST(fsync_west[0].i_MASTER_EAST_WEST())

                    #print(f"Connecting fsync_sud_id_{0} NORD_SUD OUTPUT port to fsync_root SUD INPUT port")
                    fsync_sud[0].o_MASTER_NORD_SUD(fsync_root.i_SLAVE_SUD())
                    fsync_root.o_SLAVE_SUD(fsync_sud[0].i_MASTER_NORD_SUD())

                    #print(f"Connecting fsync_east_id_{0} EAST_WEST OUTPUT port to fsync_root EAST INPUT port")
                    fsync_east[0].o_MASTER_EAST_WEST(fsync_root.i_SLAVE_EAST())
                    fsync_root.o_SLAVE_EAST(fsync_east[0].i_MASTER_EAST_WEST())

                else :  
                    #please note that last level, i.e., root level, is always even. Moreover the previous level has only one fractal NORD and one fractal SUD
                    #print(f"Connecting fsync_center_hv_lvl_{lvl-1}_id_{0} NORD_SUD OUTPUT port to fsync_root NORD INPUT port")
                    fsync_center_hv[lvl-1][0].o_MASTER_NORD_SUD(fsync_root.i_SLAVE_NORD())
                    fsync_root.o_SLAVE_NORD(fsync_center_hv[lvl-1][0].i_MASTER_NORD_SUD())

                    #print(f"Connecting fsync_center_v_lvl_{lvl-1}_id_{0} EAST_WEST OUTPUT port to fsync_root WEST INPUT port")
                    fsync_center_v[lvl-1][0].o_MASTER_EAST_WEST(fsync_root.i_SLAVE_WEST())
                    fsync_root.o_SLAVE_WEST(fsync_center_v[lvl-1][0].i_MASTER_EAST_WEST())

                    #print(f"Connecting fsync_center_hv_lvl_{lvl-1}_id_{1} NORD_SUD OUTPUT port to fsync_root SUD INPUT port")
                    fsync_center_hv[lvl-1][1].o_MASTER_NORD_SUD(fsync_root.i_SLAVE_SUD())
                    fsync_root.o_SLAVE_SUD(fsync_center_hv[lvl-1][1].i_MASTER_NORD_SUD())

                    #print(f"Connecting fsync_center_v_lvl_{lvl-1}_id_{1} EAST_WEST OUTPUT port to fsync_root EAST INPUT port")
                    fsync_center_v[lvl-1][1].o_MASTER_EAST_WEST(fsync_root.i_SLAVE_EAST())
                    fsync_root.o_SLAVE_EAST(fsync_center_v[lvl-1][1].i_MASTER_EAST_WEST())

        # Bind loader
        for id in range(0,MagiaArch.NB_CLUSTERS):
            if (id == 0):
                loader.o_OUT(cluster[id].i_LOADER()) #only cluster connected to the corner loads the elf
            loader.o_START(cluster[id].i_FETCHEN())
            loader.o_ENTRY(cluster[id].i_ENTRY())