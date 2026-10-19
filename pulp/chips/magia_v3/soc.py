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
import vp.clock_domain
from memory.memory_v3 import Memory, MemoryV3Config
from interco.router_v2 import Router, RouterConfig, RouterMapping, KIND_UNTIMED, KIND_BANDWIDTH
from utils.loader.loader_v2 import ElfLoader

from pulp.chips.magia_v3.tile import MagiaV3Tile
from pulp.chips.magia_v3.arch import *

from pulp.floonoc_v2.floonoc_v2 import FlooNocV22dMeshNarrowWide, FlooNocV2Direction
from pulp.chips.magia_v3.fractal_sync.fractal_sync import *
from pulp.chips.magia_v3.kill_module.kill_module import *
from pulp.chips.magia_v3.l2_port.l2_port import L2Port
from pulp.pcie_vfio_bridge.pcie_vfio_mem_bridge_v2 import PCIeVfioMemBridgeV2
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

class MagiaV3Soc(gvsoc.systree.Component):
    def __init__(self, parent, name, tree, parser, binary=None):
        super().__init__(parent, name)

        self.set_attributes(tree)

        # Standard platform level bin Loader
        ctrl_core_loader=ElfLoader(self, f'ctrl_core_loader', binary=binary)
        self.ctrl_core_loader = ctrl_core_loader # this is needed when building the platform as it requires at least one binary

        # Simulation engine killer, with one input per tile
        killer=KillModule(self,'kill-module',kill_addr_base=MagiaArch.TEST_END_ADDR_START,kill_addr_size=MagiaArch.TEST_END_SIZE,nb_cores_to_wait=tree.nb_clusters,
                          nb_inputs=tree.nb_clusters, done_irq_enable=MagiaArch.ENABLE_PCIE_VFIO)

        # Single clock domain
        clock = vp.clock_domain.Clock_domain(self, 'tile-clock',
                                             frequency=MagiaArch.TILE_CLK_FREQ)
        clock.o_CLOCK(self.i_CLOCK())

        # Create Tiles
        cluster:List[MagiaV3Tile] = []
        for id in range(0,tree.nb_clusters):
            cluster.append(MagiaV3Tile(self, f'magia-tile-{id}', tree, parser, id))

        # L2. No address truncation: l2_size is not necessarily a power of two,
        # and the NoC already rebases the accesses (rm_base on the L2 mapping).
        l2_mem = Memory(self, f'L2-mem', config=MemoryV3Config(
            size=tree.l2_size, latency=MagiaDSE.SOC_L2_LATENCY, truncate=False))

        # L2 fan-in: the NoC has one narrow and one wide output per row and the
        # memory has a single input, so the 2*n_tiles_y channels are joined by
        # an untimed router, which decodes and forwards with no timing of its
        # own. Each NoC channel reaches it through the beat-to-single-request
        # adapter the framework inserts.
        l2_xbar = Router(self, 'L2-xbar', config=RouterConfig(kind=KIND_UNTIMED))
        l2_xbar.o_MAP_DEFAULT(l2_mem.i_INPUT(), name='L2-mem')

        # PCIe/VFIO bridge: the host loads the binaries into the L2 and starts
        # the tiles through it instead of the ELF loader. It reaches the L2 on
        # one more input of the L2 fan-in, with addresses relative to the L2,
        # as the NoC.
        if MagiaArch.ENABLE_PCIE_VFIO:
            pcie_ep = PCIeVfioMemBridgeV2(self, 'l2-vfio-bridge',
                socket_path='/tmp/gvsoc.sock', bar0_size=0x1000, dma_chunk_bytes=16)
            pcie_ep.o_MEM(l2_xbar.i_INPUT(2*tree.n_tiles_y))
            killer.o_IRQ_DONE(pcie_ep.i_IRQ_DONE())

        # The ELF loader writes the L2 directly, through an untimed router that
        # strips the L2 base address
        l2_loader_xbar = Router(self, 'L2-loader-xbar', config=RouterConfig(kind=KIND_UNTIMED))
        l2_loader_xbar.o_MAP(l2_xbar.i_INPUT(1), RouterMapping(base=MagiaArch.L2_ADDR_START,
            size=tree.l2_size, remove_base=True), name='L2')
        l2_loader_xbar.add_mapping('error')

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
        tile_matrix: List[List[int]] = [[0 for _ in range(tree.n_tiles_x)] for _ in range(tree.n_tiles_y)]
        # Populate matrix
        id=0
        for y in range(0,tree.n_tiles_y):
            for x in range(0,tree.n_tiles_x):
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
        for n_fractal in n_fract_per_lvl(tree.nb_clusters):
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
        if tree.nb_clusters >= 4:
            n_fractal_neighbour=(((tree.n_tiles_x)//2) - 1)*(tree.n_tiles_y)
            print(f"Placing {n_fractal_neighbour*2} neighbour fsync at level 0")
            for n_fractal in range(0,n_fractal_neighbour):
                fsync_neighbour_east_west.append(FractalSync(self,f'fsync_east_west_nb_id_{n_fractal}',level=0))
                fsync_neighbour_nord_sud.append(FractalSync(self,f'fsync_nord_sud_nb_id_{n_fractal}',level=0))


        #Connect NoC to tiles and L2    
        # max_burst_size is the 4 KiB AXI page: the NI checks that no target
        # range splits a page, which holds since the tile L1 windows and the L2
        # window are whole pages, so a burst always has a single destination.
        # As in the RTL, the routers have input FIFOs of 2 entries (floo_nw_router
        # InFifoDepth).
        noc = FlooNocV22dMeshNarrowWide(self,
                                    name='magia-noc',
                                    narrow_width=MagiaArch.BYTES_PER_WORD,
                                    wide_width=MagiaArch.TILE_WIDE_WIDTH,
                                    # ChimneyDefaultCfg MaxTxns
                                    ni_outstanding_reqs=32,
                                    router_input_queue_size=2,
                                    dim_x=tree.n_tiles_x+1, dim_y=tree.n_tiles_y,
                                    max_burst_size=4096)


        # Create noc routers
        for y in range(0,tree.n_tiles_y):
            for x in range(1,tree.n_tiles_x+1):
                print(f"[NoC] Adding router and NI at position x={x} y={y}")
                noc.add_router(x, y)
                noc.add_network_interface(x, y)

        for y in range(0,tree.n_tiles_y):
            print(f"[NoC] L2-NI at position x={0} y={y}")
            noc.add_network_interface(0, y)

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
        for y in range(0,tree.n_tiles_y):
            for x in range(1,tree.n_tiles_x+1):
                print(f"[NoC] Adding cluster {id} at position x={x} y={y}")
                cluster[id].o_KILLER_OUTPUT(killer.i_INPUT(id))
                cluster[id].o_NARROW_OUTPUT(noc.i_NARROW_INPUT(x,y))
                # The NI mapping table is shared by both channels, so the tile's
                # L1 range is declared once, with the narrow binding, and the wide
                # output only needs its binding
                noc.o_NARROW_MAP(cluster[id].i_NARROW_INPUT(),name=f'tile-{id}-l1-mem',base=MagiaArch.L1_ADDR_START+(id*MagiaArch.L1_TILE_OFFSET),size=MagiaArch.L1_SIZE,x=x,y=y,rm_base=False)
                cluster[id].o_WIDE_OUTPUT(noc.i_WIDE_INPUT(x,y))
                noc.o_WIDE_BIND(cluster[id].i_WIDE_INPUT(),x=x,y=y)
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

        for y in range(0,tree.n_tiles_y):
            print(f"[NoC] Adding L2 at position x={0} y={y}")
            # Narrow port of the L2: the axi_dw_upsizer of the testbench, which
            # delays the AR by two cycles and serves one read at a time for the
            # whole row: the next AR is taken the cycle after the last R beat of
            # the previous one (ar_valid held meanwhile on the RTL waveforms,
            # the reads of all the tiles of the row reaching it through the L2
            # chimney). With 8 tiles per row it bounds the L2 reads of the row.
            l2_narrow_port = L2Port(self, f'L2-narrow-port-{y}', width=MagiaArch.BYTES_PER_WORD,
                req_latency=MagiaDSE.SOC_L2_NARROW_UPSIZER_LATENCY, max_reads=1)
            l2_narrow_port.o_OUTPUT(l2_xbar.i_INPUT(2*y))
            noc.o_NARROW_BIND(l2_narrow_port.i_INPUT(), x=0, y=y)
            # Wide port of the L2: the axi_sim_mem, reached directly
            l2_wide_port = L2Port(self, f'L2-wide-port-{y}', width=MagiaArch.TILE_WIDE_WIDTH)
            l2_wide_port.o_OUTPUT(l2_xbar.i_INPUT(2*y + 1))
            noc.o_WIDE_BIND(l2_wide_port.i_INPUT(), x=0, y=y)

        noc.o_MAP_DIR(base=MagiaArch.L2_ADDR_START,size=tree.l2_size, dir=FlooNocV2Direction.LEFT,name=f'mem_left', rm_base=True)

        # Fractal tree routing
        for lvl in range(0,int(math.log2(tree.nb_clusters))):
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
                            print(f"Connection tile-id {id+tree.n_tiles_x} to fsync_neighbour_nord_sud_{n} SUD INPUT port")
                            cluster[id+tree.n_tiles_x].o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(fsync_neighbour_nord_sud[n].i_SLAVE_SUD())
                            fsync_neighbour_nord_sud[n].o_SLAVE_SUD(cluster[id+tree.n_tiles_x].i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL())
                            n=n+1
    
            elif (lvl == 1) and (lvl<(int(math.log2(tree.nb_clusters))-1)): #this is another special level as from now on we leave the nord-sud naming and we move to a more abstract form
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
            
            elif (lvl > 1) and (lvl<(int(math.log2(tree.nb_clusters))-1)): # intermediate levels
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

        # Bind the loader, or the PCIe/VFIO bridge, to the clusters
        for id in range(0,tree.nb_clusters):
            if MagiaArch.ENABLE_PCIE_VFIO:
                pcie_ep.o_FETCH_ENABLE(cluster[id].i_FETCHEN())
                pcie_ep.o_ENTRY_ADDR(cluster[id].i_ENTRY())
            else:
                if (id == 0):
                    ctrl_core_loader.o_OUT(l2_loader_xbar.i_INPUT())
                ctrl_core_loader.o_START(cluster[id].i_FETCHEN())
                ctrl_core_loader.o_ENTRY(cluster[id].i_ENTRY())
