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
# Discription: This file is the GVSoC configuration file for the TeraNoc System.
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
#         Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)

import memory.memory as memory
from vp.clock_domain import Clock_domain
import interco.router as router
import devices.uart.ns16550 as ns16550
import utils.loader.loader
import gvsoc.systree as st
from pulp.mempool.mempool_dma import MemPoolDma
from elftools.elf.elffile import *
from interco.converter import Converter
from pulp.teranoc.teranoc_cluster import Cluster
from pulp.teranoc.ctrl_registers import CtrlRegisters
from pulp.teranoc.l2_subsystem import L2_subsystem
from pulp.teranoc.l2_interconnect.l2_address_scrambler import L2_AddressScrambler
from pulp.teranoc.l2_interconnect.l2_noc import L2_noc

class System(st.Component):

    def __init__(self, parent, name, parser, terapool: bool=True, nb_cores_per_tile: int=4, nb_x_groups: int=4, nb_y_groups: int=4, total_cores: int=1024, nb_remote_ports_per_tile: int=2, bank_factor: int=4, axi_data_width: int=64, nb_axi_masters_per_group: int=1, l2_size: int=0x1000000, nb_l2_banks: int=16):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################

        [args, __] = parser.parse_known_args()

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        nb_groups = nb_x_groups * nb_y_groups
        nb_axi_masters = nb_axi_masters_per_group * nb_groups
        l2_bank_size = l2_size // nb_l2_banks

        ################################################################
        ##########              Design Components             ##########
        ################################################################ 

        #Mempool cluster
        mempool_cluster=Cluster(self, 'mempool_cluster', parser=parser, nb_cores_per_tile=nb_cores_per_tile, \
            nb_x_groups=nb_x_groups, nb_y_groups=nb_y_groups, total_cores=total_cores, nb_remote_ports_per_tile=nb_remote_ports_per_tile, \
            bank_factor=bank_factor, axi_data_width=axi_data_width, nb_axi_masters_per_group=nb_axi_masters_per_group, \
            l2_size=l2_size, nb_l2_banks=nb_l2_banks)

        # Boot Rom
        rom = memory.Memory(self, 'rom', size=0x1000, width_log2=(axi_data_width - 1).bit_length(), stim_file=self.get_file_path('pulp/chips/spatz/rom.bin'))

        # L2 NoC
        l2_noc = L2_noc(self, 'l2_noc', width=axi_data_width, nb_x_groups=nb_x_groups, nb_y_groups=nb_y_groups, ni_outstanding_reqs=32, router_input_queue_size=2)

        # L2 Memory
        # Efficient bandwidth of each port is only 1/4 of axi_data_width in current design
        l2_mem = L2_subsystem(self, 'l2_mem', nb_banks=nb_l2_banks, bank_width=axi_data_width, size=l2_size, port_bandwidth=axi_data_width//4)

        # CSR
        csr = CtrlRegisters(self, 'ctrl_registers', wakeup_latency=5)

        # UART        
        uart = ns16550.Ns16550(self, 'uart')

        # DMA
        dma = MemPoolDma(self, 'dma', loc_base=0x0, loc_size=0x400000, tcdm_width=total_cores*bank_factor*4)

        # Binary Loader
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry=0x80000000)

        # L2 loader converter
        l2_loader_converter = Converter(self, 'l2_loader_converter', output_width=axi_data_width*16, output_align=axi_data_width*16)

        # L2 loader address scrambler
        l2_loader_scrambler = L2_AddressScrambler(self, 'l2_loader_scrambler', bypass=False, l2_base_addr=0x0, l2_size=l2_size, nb_banks=nb_l2_banks, bank_width=axi_data_width, interleave=16)

        #Dummy Memory
        dummy_mem = memory.Memory(self, 'dummy_mem', atomics=True, size=0x400000)

        # AXI Interconnect
        # axi_ico = []
        # for i in range(0, nb_axi_masters):
        #     axi_ico.append(router.Router(self, f'axi_ico_{i}', latency=0))
        #     axi_ico[i].add_mapping('l2', base=0x80000000, remove_offset=0x80000000, size=0x1000000)
        #     axi_ico[i].add_mapping('soc')       

        soc_ico = router.Router(self, 'soc_ico')    # TODO: output bandwidth only
        soc_ico.add_mapping('bootrom', base=0xa0000000, remove_offset=0xa0000000, size=0x10000, latency=1)
        soc_ico.add_mapping('peripheral', base=0x40000000, size=0x20000, latency=1)
        soc_ico.add_mapping('external', latency=1)

        periph_ico = router.Router(self, 'periph_ico', bandwidth=4)
        periph_ico.add_mapping('csr', base=0x40000000, remove_offset=0x40000000, size=0x10000, latency=1)
        periph_ico.add_mapping('dma_ctrl', base=0x40010000, remove_offset=0x40010000, size=0x10000, latency=1)

        ext_ico = router.Router(self, 'ext_ico', bandwidth=4)
        ext_ico.add_mapping('uart', base=0xc0000000, remove_offset=0xc0000000, size=0x100, latency=1)

        # Binary Loader Router
        loader_router = router.Router(self, 'loader_router', bandwidth=32, latency=1)
        loader_router.add_mapping('output')

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################

        # Group axi port -> L2 NoC
        for i in range(0, nb_x_groups):
            for j in range(0, nb_y_groups):
                self.bind(mempool_cluster, f'axi_{i}_{j}_0', l2_noc, f'wide_input_{i+1}_{j+1}')

        # L2 NoC -> HBM and peripherals
        if terapool:
            hbm5_soc_demux = router.Router(self, 'hbm5_soc_demux', bandwidth=axi_data_width, latency=4)
            hbm5_soc_demux.add_mapping('hbm5', base=0x80000000+l2_bank_size*5, size=l2_bank_size, remove_offset=0x80000000+l2_bank_size*5)
            hbm5_soc_demux.add_mapping('soc')
            
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(0),  base=0x80000000+l2_bank_size*0,  size=l2_bank_size, x=0, y=1, name="HBM0",  rm_base=True)  # HBM bank 0
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(1),  base=0x80000000+l2_bank_size*1,  size=l2_bank_size, x=0, y=2, name="HBM1",  rm_base=True)  # HBM bank 1
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(2),  base=0x80000000+l2_bank_size*2,  size=l2_bank_size, x=0, y=3, name="HBM2",  rm_base=True)  # HBM bank 2
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(3),  base=0x80000000+l2_bank_size*3,  size=l2_bank_size, x=0, y=4, name="HBM3",  rm_base=True)  # HBM bank 3
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(4),  base=0x80000000+l2_bank_size*4,  size=l2_bank_size, x=2, y=0, name="HBM4",  rm_base=True)  # HBM bank 4
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(6),  base=0x80000000+l2_bank_size*6,  size=l2_bank_size, x=1, y=5, name="HBM6",  rm_base=True)  # HBM bank 6
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(7),  base=0x80000000+l2_bank_size*7,  size=l2_bank_size, x=2, y=5, name="HBM7",  rm_base=True)  # HBM bank 7
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(8),  base=0x80000000+l2_bank_size*8,  size=l2_bank_size, x=3, y=0, name="HBM8",  rm_base=True)  # HBM bank 8
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(9),  base=0x80000000+l2_bank_size*9,  size=l2_bank_size, x=4, y=0, name="HBM9",  rm_base=True)  # HBM bank 9
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(10), base=0x80000000+l2_bank_size*10, size=l2_bank_size, x=4, y=5, name="HBM10", rm_base=True)  # HBM bank 10
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(11), base=0x80000000+l2_bank_size*11, size=l2_bank_size, x=3, y=5, name="HBM11", rm_base=True)  # HBM bank 11
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(12), base=0x80000000+l2_bank_size*12, size=l2_bank_size, x=5, y=1, name="HBM12", rm_base=True)  # HBM bank 12
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(13), base=0x80000000+l2_bank_size*13, size=l2_bank_size, x=5, y=2, name="HBM13", rm_base=True)  # HBM bank 13
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(14), base=0x80000000+l2_bank_size*14, size=l2_bank_size, x=5, y=3, name="HBM14", rm_base=True)  # HBM bank 14
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(15), base=0x80000000+l2_bank_size*15, size=l2_bank_size, x=5, y=4, name="HBM15", rm_base=True)  # HBM bank 15
            l2_noc.o_WIDE_MAP(hbm5_soc_demux.i_INPUT(0), base=0x80000000+l2_bank_size*5, size=l2_bank_size, x=1, y=0, name="HBM5_SoC", rm_base=False)  # HBM bank 5 + soc
            l2_noc.add_mapping(name="soc0", base=0x00000000, size=0x80000000, x=1, y=0, rm_base=False)
            l2_noc.add_mapping(name="soc1", base=0xA0000000, size=0x30000000, x=1, y=0, rm_base=False)

            self.bind(hbm5_soc_demux, 'hbm5', l2_mem, 'input_5')
            self.bind(hbm5_soc_demux, 'soc', soc_ico, 'input')

        else:
            soc_demux = router.Router(self, 'soc_demux', bandwidth=axi_data_width, latency=4)
            soc_demux.add_mapping('soc')

            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(0), base=0x80000000+l2_bank_size*0, size=l2_bank_size, x=0, y=1, name="HBM0", rm_base=True)  # HBM bank 0
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(1), base=0x80000000+l2_bank_size*1, size=l2_bank_size, x=0, y=2, name="HBM1", rm_base=True)  # HBM bank 1
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(2), base=0x80000000+l2_bank_size*2, size=l2_bank_size, x=3, y=2, name="HBM2", rm_base=True)  # HBM bank 2
            l2_noc.o_WIDE_MAP(l2_mem.i_BANK_INPUT(3), base=0x80000000+l2_bank_size*3, size=l2_bank_size, x=3, y=1, name="HBM3", rm_base=True)  # HBM bank 3
            l2_noc.o_WIDE_MAP(soc_demux.i_INPUT(0), base=0x00000000, size=0x80000000, x=1, y=0, name="soc0", rm_base=False)  # soc
            l2_noc.add_mapping(name="soc1", base=0xA0000000, size=0x30000000, x=1, y=0, rm_base=False)

            self.bind(soc_demux, 'soc', soc_ico, 'input')

        # SoC interconnect
        # for i in range(0, nb_axi_masters):
        #     self.bind(axi_ico[i], 'soc', soc_ico, 'input')

        # Peripheral interconnect
        self.bind(soc_ico, 'peripheral', periph_ico, 'input')

        # External interconnect
        self.bind(soc_ico, 'external', ext_ico, 'input')

        # Bootrom
        self.bind(soc_ico, 'bootrom', rom, 'input')

        # L2
        # for i in range(0, nb_axi_masters):
        #     self.bind(axi_ico[i], 'l2', l2_mem, 'input_%d' % i)

        # CSR
        self.bind(periph_ico, 'csr', csr, 'input')

        # DMA Ctrl
        self.bind(periph_ico, 'dma_ctrl', dma, 'input')

        # UART
        self.bind(ext_ico, 'uart', uart, 'input')

        #loader router
        self.bind(loader, 'start', mempool_cluster, 'loader_start')
        self.bind(loader, 'entry', mempool_cluster, 'loader_entry')
        self.bind(loader, 'out', loader_router, 'input')
        loader_router.add_mapping('dummy', base=0x00000000, remove_offset=0x00000000, size=0x400000)
        loader_router.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=l2_size)
        loader_router.add_mapping('rom', base=0xa0000000, remove_offset=0xa0000000, size=0x1000)
        loader_router.add_mapping('csr', base=0x40000000, remove_offset=0x40000000, size=0x10000)
        self.bind(loader_router, 'mem', l2_loader_converter, 'input')
        self.bind(l2_loader_converter, 'out', l2_loader_scrambler, 'input') 
        self.bind(l2_loader_scrambler, 'output', l2_mem, 'input_loader')
        self.bind(loader_router, 'rom', rom, 'input')
        self.bind(loader_router, 'csr', csr, 'input')
        self.bind(loader_router, 'dummy', dummy_mem, 'input')

        #Cluster Registers for synchronization barrier
        for i in range(0, total_cores):
            self.bind(csr, f'barrier_ack', mempool_cluster, f'barrier_ack_{i}')

        #L2 ro-cache configuration
        self.bind(csr, 'rocache_cfg', mempool_cluster, 'rocache_cfg')

        #DMA data
        #To emulate distributed backends in groups
        # self.bind(dma, 'axi_read', mempool_cluster, 'dma_axi')
        # self.bind(dma, 'axi_write', mempool_cluster, 'dma_axi')
        # DMA via L2 interconnect not implemented yet
        self.bind(dma, 'axi_read', loader_router, 'input')
        self.bind(dma, 'axi_write', loader_router, 'input')
        self.bind(dma, 'tcdm_read', mempool_cluster, 'dma_tcdm')
        self.bind(dma, 'tcdm_write', mempool_cluster, 'dma_tcdm')

class TeranocSystem(st.Component):

    def __init__(self, parent, name, parser, options):

        super(TeranocSystem, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=500000000)

        soc = System(self, 'teranoc_soc', parser, terapool=True, nb_cores_per_tile=4, nb_x_groups=4, nb_y_groups=4, total_cores=1024, nb_remote_ports_per_tile=2, bank_factor=4, axi_data_width=64, nb_axi_masters_per_group=1, l2_size=0x1000000, nb_l2_banks=16)

        self.bind(clock, 'out', soc, 'clock')

class MempoolNocSystem(st.Component):

    def __init__(self, parent, name, parser, options):

        super(MempoolNocSystem, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=500000000)

        soc = System(self, 'teranoc_soc', parser, terapool=False, nb_cores_per_tile=4, nb_x_groups=2, nb_y_groups=2, total_cores=256, nb_remote_ports_per_tile=2, bank_factor=4, axi_data_width=64, nb_axi_masters_per_group=1, l2_size=0x400000, nb_l2_banks=4)

        self.bind(clock, 'out', soc, 'clock')

class MinpoolNocSystem(st.Component):
    
    def __init__(self, parent, name, parser, options):

        super(MinpoolNocSystem, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=500000000)

        soc = System(self, 'teranoc_soc', parser, terapool=False, nb_cores_per_tile=4, nb_x_groups=2, nb_y_groups=2, total_cores=16, nb_remote_ports_per_tile=2, bank_factor=4, axi_data_width=32, nb_axi_masters_per_group=1, l2_size=0x400000, nb_l2_banks=4)

        self.bind(clock, 'out', soc, 'clock')
