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
from pulp.mempool.dma.mempool_dma_top import MemPoolDmaTop
from elftools.elf.elffile import *
from interco.converter import Converter
from pulp.teranoc.teranoc_cluster import TeranocCluster
from pulp.teranoc.ctrl_registers import CtrlRegisters
from pulp.teranoc.l2_subsystem import L2_subsystem
from pulp.teranoc.l2_interconnect.l2_address_scrambler import L2AddressScrambler
from pulp.teranoc.l2_interconnect.l2_noc import L2_noc
from pulp.teranoc.arch import CONFIGS, DEFAULT_CONFIG


class TeranocSystem(st.Component):

    def __init__(self, parent, name, parser, arch, binary=None):
        super().__init__(parent, name)

        # Convenience locals for the bits we use heavily below; everything
        # else is read straight from `arch`.
        axi_data_width     = arch.axi_data_width
        nb_banks_per_group = arch.nb_banks_per_group
        l2_bank_size       = arch.l2_bank_size

        ################################################################
        ##########              Design Components             ##########
        ################################################################

        # TeraNoC cluster
        teranoc_cluster = TeranocCluster(self, 'teranoc_cluster', parser=parser, arch=arch)

        # Boot Rom
        rom = memory.Memory(self, 'rom', size=0x1000, width_log2=(axi_data_width - 1).bit_length(), stim_file=self.get_file_path('pulp/chips/spatz/rom.bin'))

        # L2 NoC
        l2_noc = L2_noc(self, 'l2_noc', width=axi_data_width, nb_x_groups=arch.nb_x_groups, nb_y_groups=arch.nb_y_groups, ni_outstanding_reqs=32, router_input_queue_size=2)

        # L2 Memory
        l2_mem = L2_subsystem(self, 'l2_mem', nb_banks=arch.nb_l2_banks, bank_width=axi_data_width, size=arch.l2_size, port_bandwidth=axi_data_width)

        # CSR
        csr = CtrlRegisters(self, 'ctrl_registers', wakeup_latency=5)

        # UART
        uart = ns16550.Ns16550(self, 'uart')

        # DMA
        dma = MemPoolDmaTop(self, 'dma', loc_base=0x0, loc_size=0x400000, burst_size=4*nb_banks_per_group, tcdm_width=4*nb_banks_per_group,
                            nb_groups=arch.nb_groups, nb_dmas_per_group=1, be_width=4*nb_banks_per_group)

        # Binary Loader
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry=0x80000000)

        # L2 loader converter
        l2_loader_converter = Converter(self, 'l2_loader_converter', output_width=axi_data_width*16, output_align=axi_data_width*16)

        # L2 loader address scrambler
        l2_loader_scrambler = L2AddressScrambler(self, 'l2_loader_scrambler', bypass=False, l2_base_addr=0x0, l2_size=arch.l2_size, nb_banks=arch.nb_l2_banks, bank_width=axi_data_width, interleave=16)

        #Dummy Memory
        dummy_mem = memory.Memory(self, 'dummy_mem', atomics=True, size=0x400000)

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
        for i in range(0, arch.nb_x_groups):
            for j in range(0, arch.nb_y_groups):
                self.bind(teranoc_cluster, f'axi_{i}_{j}_0', l2_noc, f'wide_input_{i+1}_{j+1}')

        # L2 NoC -> HBM and peripherals
        if arch.nb_x_groups == 2 and arch.nb_y_groups == 2:
            soc_demux = router.Router(self, 'soc_demux', bandwidth=axi_data_width, latency=4)
            soc_demux.add_mapping('soc')

            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(0), x=0, y=1)  # HBM bank 0
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(1), x=0, y=2)  # HBM bank 1
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(2), x=3, y=2)  # HBM bank 2
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(3), x=3, y=1)  # HBM bank 3
            l2_noc.o_WIDE_BIND(soc_demux.i_INPUT(0),   x=1, y=0)  # soc

            l2_noc.o_MAP(base=0x80000000+l2_bank_size*0, size=l2_bank_size, x=0, y=1, name='hbm0', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*1, size=l2_bank_size, x=0, y=2, name='hbm1', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*2, size=l2_bank_size, x=3, y=2, name='hbm2', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*3, size=l2_bank_size, x=3, y=1, name='hbm3', rm_base=True)
            l2_noc.o_MAP(base=0x00000000,                size=0x80000000,   x=1, y=0, name='soc1', rm_base=False)
            l2_noc.o_MAP(base=0xA0000000,                size=0x30000000,   x=1, y=0, name='soc2', rm_base=False)

            self.bind(soc_demux, 'soc', soc_ico, 'input')

        elif arch.nb_x_groups == 4 and arch.nb_y_groups == 4:
            hbm5_soc_demux = router.Router(self, 'hbm5_soc_demux', bandwidth=axi_data_width, latency=4)
            hbm5_soc_demux.add_mapping('hbm5', base=0x80000000+l2_bank_size*5, size=l2_bank_size, remove_offset=0x80000000+l2_bank_size*5)
            hbm5_soc_demux.add_mapping('soc')

            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(0),    x=0, y=1)  # HBM bank 0
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(1),    x=0, y=2)  # HBM bank 1
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(2),    x=0, y=3)  # HBM bank 2
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(3),    x=0, y=4)  # HBM bank 3
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(4),    x=2, y=0)  # HBM bank 4
            l2_noc.o_WIDE_BIND(hbm5_soc_demux.i_INPUT(0), x=1, y=0)  # HBM bank 5 + soc
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(6),    x=1, y=5)  # HBM bank 6
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(7),    x=2, y=5)  # HBM bank 7
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(8),    x=3, y=0)  # HBM bank 8
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(9),    x=4, y=0)  # HBM bank 9
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(10),   x=4, y=5)  # HBM bank 10
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(11),   x=3, y=5)  # HBM bank 11
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(12),   x=5, y=1)  # HBM bank 12
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(13),   x=5, y=2)  # HBM bank 13
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(14),   x=5, y=3)  # HBM bank 14
            l2_noc.o_WIDE_BIND(l2_mem.i_BANK_INPUT(15),   x=5, y=4)  # HBM bank 15

            l2_noc.o_MAP(base=0x80000000+l2_bank_size*0,  size=l2_bank_size, x=0, y=1, name='hbm0', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*1,  size=l2_bank_size, x=0, y=2, name='hbm1', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*2,  size=l2_bank_size, x=0, y=3, name='hbm2', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*3,  size=l2_bank_size, x=0, y=4, name='hbm3', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*4,  size=l2_bank_size, x=2, y=0, name='hbm4', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*5,  size=l2_bank_size, x=1, y=0, name='hbm5', rm_base=False)
            l2_noc.o_MAP(base=0x00000000,                 size=0x80000000,   x=1, y=0, name='soc1', rm_base=False)
            l2_noc.o_MAP(base=0xA0000000,                 size=0x30000000,   x=1, y=0, name='soc2', rm_base=False)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*6,  size=l2_bank_size, x=1, y=5, name='hbm6', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*7,  size=l2_bank_size, x=2, y=5, name='hbm7', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*8,  size=l2_bank_size, x=3, y=0, name='hbm8', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*9,  size=l2_bank_size, x=4, y=0, name='hbm9', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*10, size=l2_bank_size, x=4, y=5, name='hbm10', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*11, size=l2_bank_size, x=3, y=5, name='hbm11', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*12, size=l2_bank_size, x=5, y=1, name='hbm12', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*13, size=l2_bank_size, x=5, y=2, name='hbm13', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*14, size=l2_bank_size, x=5, y=3, name='hbm14', rm_base=True)
            l2_noc.o_MAP(base=0x80000000+l2_bank_size*15, size=l2_bank_size, x=5, y=4, name='hbm15', rm_base=True)

            self.bind(hbm5_soc_demux, 'hbm5', l2_mem, 'input_5')
            self.bind(hbm5_soc_demux, 'soc', soc_ico, 'input')

        # Peripheral interconnect
        self.bind(soc_ico, 'peripheral', periph_ico, 'input')

        # External interconnect
        self.bind(soc_ico, 'external', ext_ico, 'input')

        # Bootrom
        self.bind(soc_ico, 'bootrom', rom, 'input')

        # CSR
        self.bind(periph_ico, 'csr', csr, 'input')

        # DMA Ctrl
        self.bind(periph_ico, 'dma_ctrl', dma, 'input')

        # UART
        self.bind(ext_ico, 'uart', uart, 'input')

        #loader router
        self.bind(loader, 'start', teranoc_cluster, 'loader_start')
        self.bind(loader, 'entry', teranoc_cluster, 'loader_entry')
        self.bind(loader, 'out', loader_router, 'input')
        loader_router.add_mapping('dummy', base=0x00000000, remove_offset=0x00000000, size=0x400000)
        loader_router.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=arch.l2_size)
        loader_router.add_mapping('rom', base=0xa0000000, remove_offset=0xa0000000, size=0x1000)
        loader_router.add_mapping('csr', base=0x40000000, remove_offset=0x40000000, size=0x10000)
        self.bind(loader_router, 'mem', l2_loader_converter, 'input')
        self.bind(l2_loader_converter, 'out', l2_loader_scrambler, 'input')
        self.bind(l2_loader_scrambler, 'output', l2_mem, 'input_loader')
        self.bind(loader_router, 'rom', rom, 'input')
        self.bind(loader_router, 'csr', csr, 'input')
        self.bind(loader_router, 'dummy', dummy_mem, 'input')

        #Cluster Registers for synchronization barrier
        for i in range(0, arch.total_snitch):
            self.bind(csr, f'barrier_ack', teranoc_cluster, f'barrier_ack_{i}')

        #L2 ro-cache configuration
        self.bind(csr, 'rocache_cfg', teranoc_cluster, 'rocache_cfg')

        #DMA data
        #To emulate distributed backends in groups
        for i in range(arch.nb_groups):
            self.bind(dma, f'axi_read_{i}_0', teranoc_cluster, f'dma_axi_{i}')
            self.bind(dma, f'axi_write_{i}_0', teranoc_cluster, f'dma_axi_{i}')
            self.bind(dma, f'tcdm_read_{i}_0', teranoc_cluster, f'dma_tcdm_{i}')
            self.bind(dma, f'tcdm_write_{i}_0', teranoc_cluster, f'dma_tcdm_{i}')


class TeranocSoc(st.Component):

    def __init__(self, parent, name, parser, options):
        super().__init__(parent, name, options=options)

        parser.add_argument('--config', dest='config',
            choices=list(CONFIGS.keys()), default=DEFAULT_CONFIG,
            help='Select Teranoc arch profile (default: %(default)s)')
        [args, __] = parser.parse_known_args()
        arch = CONFIGS[args.config]

        clock = Clock_domain(self, 'clock', frequency=500000000)
        system = TeranocSystem(self, 'teranoc_system', parser,
            arch=arch, binary=args.binary)
        self.bind(clock, 'out', system, 'clock')
