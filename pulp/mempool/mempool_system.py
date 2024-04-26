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
import memory.memory as memory
from pulp.snitch.hierarchical_cache import Hierarchical_cache
from vp.clock_domain import Clock_domain
import pulp.mempool.l1_subsystem as l1_subsystem
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from pulp.mempool.mempool_tile_submodule.dma_interleaver import DmaInterleaver
from interco.interleaver_snitch import Interleaver
from pulp.idma.snitch_dma import SnitchDma
from interco.bus_watchpoint import Bus_watchpoint
from interco.sequencer import Sequencer
from pulp.spatz.cluster_registers import Cluster_registers
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
import math
from pulp.mempool.mempool_cluster import Cluster

GAPY_TARGET = True

class System(st.Component):

    def __init__(self, parent, name, parser, nb_cores_per_tile: int=4, nb_groups: int=4, total_cores: int= 256, bank_factor: int=4):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################

        ################################################################
        ##########              Design Components             ##########
        ################################################################ 

        #Mempool cluster
        mempool_cluster=Cluster(self,'mempool_cluster',parser=parser, nb_cores_per_tile=nb_cores_per_tile, nb_groups=nb_groups, total_cores=total_cores, bank_factor=bank_factor)

        # Boot Rom
        rom = memory.Memory(self, 'rom', size=0x1000, width_log2=6, stim_file=self.get_file_path('pulp/chips/spatz/rom.bin'))

         # L2 Memory
        l2_mem = memory.Memory(self, 'l2_mem', size=0x1000000, width_log2=6, atomics=True, core="snitch", mem='mem')

        # Cluster peripherals
        # cluster_registers = Cluster_registers(self, 'cluster_registers', boot_addr=entry, nb_cores=nb_cores)
        # ico.add_mapping('cluster_registers', base=0x00120000, remove_offset=0x00120000, size=0x1000)

        #rom router
        rom_router = router.Router(self, 'rom_router', bandwidth=64, latency=1)
        rom_router.add_mapping('output')

        #l2 router
        l2_router = router.Router(self, 'l2_router', bandwidth=64, latency=1)
        l2_router.add_mapping('output')

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################

        #rom router
        for i in range(0, nb_groups):
            self.bind(mempool_cluster, 'rom_%d' % i, rom_router, 'input')
        self.bind(rom_router,'output',rom, 'input')

        #l2 router
        for i in range(0, nb_groups):
            self.bind(mempool_cluster, 'L2_data_%d' % i, l2_router, 'input')
        self.bind(l2_router,'output',l2_mem, 'input')

        # self.bind(mempool_cluster, 'cluster_registers', cluster_registers, 'input')

class MempoolSystem(st.Component):

    def __init__(self, parent, name, parser, options):

        super(MempoolSystem, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=500000000)

        soc = System(self, 'mempool_soc', parser)

        self.bind(clock, 'out', soc, 'clock')


