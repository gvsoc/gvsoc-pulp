#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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
# Discription: This file is the GVSoC configuration file for the MemPool Tile.
# Author: Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)

import gvsoc.runner
import cpu.iss.riscv as iss
from memory.memory import Memory
from pulp.snitch.hierarchical_cache import Hierarchical_cache
from vp.clock_domain import Clock_domain
import pulp.mempool.l1_subsystem as l1_subsystem
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from pulp.mempool.mempool_tile_submodule.dma_interleaver import DmaInterleaver
from interco.interleaver import Interleaver
from pulp.idma.snitch_dma import SnitchDma
from interco.bus_watchpoint import Bus_watchpoint
from pulp.spatz.cluster_registers import Cluster_registers
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
import math
from pulp.mempool.mempool_group import Group

GAPY_TARGET = True

class Cluster(st.Component):

    def __init__(self, parent, name, parser, nb_cores_per_tile: int=4, nb_groups: int=4, total_cores: int= 256, bank_factor: int=4):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters 
        nb_remote_ports_per_group = nb_groups - 1
        nb_tiles_per_group = int((total_cores/nb_groups)/nb_cores_per_tile)

        ################################################################
        ##########              Design Components             ##########
        ################################################################
        # TIles
        self.group_list = []
        for i in range(0, nb_groups):
            self.group_list.append(Group(self,f'group_{i}',parser=parser,group_id=i, nb_cores_per_tile=nb_cores_per_tile, 
                nb_groups=nb_groups, total_cores=total_cores, bank_factor=bank_factor))
        
        ################################################################
        ##########               Design Bindings              ##########
        ################################################################

        #Group master output -> Group slave input
        for ini in range(0, nb_groups):
            for tgt in range(0, nb_groups):
                if (ini != tgt):
                    for tile in range(0, nb_tiles_per_group):
                        debug_router=router.Router(self, 'debug_router_ini%d_tgt%d_tile%d' % (ini, tgt, tile))
                        debug_router.add_mapping("output")
                        self.bind(self.group_list[ini], f'grp_remt{ini^tgt}_tile{tile}_master_out', debug_router, 'input')
                        self.bind(debug_router, 'output', self.group_list[tgt], f'grp_remt{ini^tgt}_tile{tile}_slave_in')

        # Propagate barrier signals from group to cluster boundary
        for i in range(0, nb_groups):
            for j in range(0, nb_tiles_per_group):
                for k in range(0, nb_cores_per_tile):
                    self.bind(self, f'barrier_ack_{i*nb_cores_per_tile*nb_tiles_per_group+j*nb_cores_per_tile+k}', self.group_list[i], f'barrier_ack_{j*nb_cores_per_tile+k}')

        for i in range(0, nb_groups):
            self.bind(self.group_list[i], 'rom', self, 'rom_%d' % i)
            self.bind(self.group_list[i], 'L2_data', self, 'L2_data_%d' % i)
            self.bind(self.group_list[i], 'csr', self, 'csr_%d' % i)
            self.bind(self.group_list[i], 'dummy_mem', self, 'dummy_mem_%d' % i)
            self.bind(self, 'loader_start', self.group_list[i], 'loader_start')
            self.bind(self, 'loader_entry', self.group_list[i], 'loader_entry')
