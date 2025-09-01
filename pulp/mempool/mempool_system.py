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
# Discription: This file is the GVSoC configuration file for the MemPool System.
# Author: Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)
#         Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)

import gvsoc.runner
import cpu.iss.riscv as iss
import memory.memory as memory
from vp.clock_domain import Clock_domain
import interco.router as router
import devices.uart.ns16550 as ns16550
import utils.loader.loader
import gvsoc.systree as st
from pulp.mempool.mempool_dma import MemPoolDma
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
import math
from pulp.mempool.mempool_cluster import Cluster
from pulp.mempool.ctrl_registers import CtrlRegisters

GAPY_TARGET = True

class System(st.Component):

    def __init__(self, parent, name, parser, terapool: bool=False, nb_cores_per_tile: int=4, nb_sub_groups_per_group: int=1, nb_groups: int=4, total_cores: int= 256, bank_factor: int=4, axi_data_width: int=64):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################

        [args, __] = parser.parse_known_args()

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        ################################################################
        ##########              Design Components             ##########
        ################################################################ 

        #Mempool cluster
        mempool_cluster=Cluster(self,'mempool_cluster',terapool=terapool, parser=parser, nb_cores_per_tile=nb_cores_per_tile,
            nb_sub_groups_per_group=nb_sub_groups_per_group, nb_groups=nb_groups, total_cores=total_cores, bank_factor=bank_factor)

        # Boot Rom
        rom = memory.Memory(self, 'rom', size=0x1000, width_log2=(axi_data_width - 1).bit_length(), stim_file=self.get_file_path('pulp/chips/spatz/rom.bin'))

        # L2 Memory
        l2_mem = memory.Memory(self, 'l2_mem', size=0x1000000, width_log2=-1, atomics=True)

        # CSR
        csr = CtrlRegisters(self, 'ctrl_registers', wakeup_latency=18 if terapool else 15)
        
        uart = ns16550.Ns16550(self, 'uart')

        dma = MemPoolDma(self, 'dma', loc_base=0x0, loc_size=0x400000, tcdm_width=total_cores*bank_factor*4)
        dma_remove_offset = router.Router(self, 'dma_remove_offset')
        dma_remove_offset.add_mapping('output', base=0x80000000, remove_offset=0x80000000, size=0x1000000)

        # Binary Loader
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry=0x80000000)

        #Dummy Memory
        dummy_mem = memory.Memory(self, 'dummy_mem', atomics=True, size=0x400000)

        # Rom Router
        rom_router = router.Router(self, 'rom_router', bandwidth=axi_data_width, latency=1)
        rom_router.add_mapping('output')

        # L2 Memory Router
        l2_router = router.Router(self, 'l2_router', bandwidth=0, latency=1)
        l2_router.add_mapping('output')

        # CSR Router
        csr_router = router.Router(self, 'csr_router', bandwidth=32, latency=1)
        csr_router.add_mapping('output')
        
        # UART Router
        uart_router = router.Router(self, 'uart_router', bandwidth=8, latency=1)
        uart_router.add_mapping('output')
        
        # DMA Ctrl Router
        dma_ctrl_router = router.Router(self, 'dma_ctrl_router', bandwidth=32, latency=1)
        dma_ctrl_router.add_mapping('output')

        # Dummy Memory Router
        dummy_mem_router = router.Router(self, 'dummy_mem_router', bandwidth=32, latency=1)
        dummy_mem_router.add_mapping('output')

        # Binary Loader Router
        loader_router = router.Router(self, 'loader_router', bandwidth=32, latency=1)
        loader_router.add_mapping('output')

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

        #csr router
        for i in range(0, nb_groups):
            self.bind(mempool_cluster, 'csr_%d' % i, csr_router, 'input')
        self.bind(csr_router,'output',csr, 'input')
        
        #uart router
        for i in range(0, nb_groups):
            self.bind(mempool_cluster, 'uart_%d' % i, uart_router, 'input')
        self.bind(uart_router,'output',uart, 'input')
        
        #dma ctrl router
        for i in range(0, nb_groups):
            self.bind(mempool_cluster, 'dma_ctrl_%d' % i, dma_ctrl_router, 'input')
        self.bind(dma_ctrl_router, 'output', dma, 'input')

        #dummy_mem router
        for i in range(0, nb_groups):
            self.bind(mempool_cluster, 'dummy_mem_%d' % i, dummy_mem_router, 'input')
        self.bind(dummy_mem_router,'output',dummy_mem, 'input')

        #loader router
        self.bind(loader, 'start', mempool_cluster, 'loader_start')
        self.bind(loader, 'entry', mempool_cluster, 'loader_entry')
        self.bind(loader, 'out', loader_router, 'input')
        loader_router.add_mapping('dummy', base=0x00000000, remove_offset=0x00000000, size=0x400000)
        loader_router.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x1000000)
        loader_router.add_mapping('rom', base=0xa0000000, remove_offset=0xa0000000, size=0x1000)
        loader_router.add_mapping('csr', base=0x40000000, remove_offset=0x40000000, size=0x10000)
        self.bind(loader_router, 'mem', l2_mem, 'input')
        self.bind(loader_router, 'rom', rom, 'input')
        self.bind(loader_router, 'csr', csr, 'input')
        self.bind(loader_router, 'dummy', dummy_mem, 'input')

        #Cluster Registers for synchronization barrier
        for i in range(0, total_cores):
            self.bind(csr, f'barrier_ack', mempool_cluster, f'barrier_ack_{i}')

        #dma data
        # self.bind(dma, 'axi_read', l2_router, 'input')
        # self.bind(dma, 'axi_write', l2_router, 'input')
        self.bind(dma, 'axi_read', dma_remove_offset, 'input')
        self.bind(dma, 'axi_write', dma_remove_offset, 'input')
        self.bind(dma_remove_offset, 'output', l2_router, 'input')

        self.bind(dma, 'tcdm_read', mempool_cluster, 'dma_tcdm')
        self.bind(dma, 'tcdm_write', mempool_cluster, 'dma_tcdm')

class MempoolSystem(st.Component):

    def __init__(self, parent, name, parser, options):

        super(MempoolSystem, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=500000000)

        soc = System(self, 'mempool_soc', parser)

        self.bind(clock, 'out', soc, 'clock')

class MinpoolSystem(st.Component):

    def __init__(self, parent, name, parser, options):

        super(MinpoolSystem, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=500000000)

        soc = System(self, 'mempool_soc', parser, nb_cores_per_tile=4, nb_groups=4, total_cores=16, bank_factor=4, axi_data_width=32)

        self.bind(clock, 'out', soc, 'clock')

class TerapoolSystem(st.Component):
    
    def __init__(self, parent, name, parser, options):

        super(TerapoolSystem, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=500000000)

        soc = System(self, 'mempool_soc', parser, terapool=True, nb_cores_per_tile=8, nb_sub_groups_per_group=4, nb_groups=4, total_cores=1024, bank_factor=4, axi_data_width=64)

        self.bind(clock, 'out', soc, 'clock')
