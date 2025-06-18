#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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

import gvsoc.systree as st
import memory.memory as memory
import interco.router as router
import devices.uart.ns16550 as ns16550
import cpu.clint
import cpu.plic
import utils.loader.loader
import pulp.chips.cheshire.cva6.cva6 as cva6
import pulp.chips.cheshire.soc_regs as soc_regs
from pulp.stdout.stdout_v3 import Stdout
from pulp.idma.idma import IDma
import cache.cache as cache
from pulp.icache_ctrl.icache_ctrl_v2 import Icache_ctrl
import pulp.gpio.gpio_v3 as gpio_module
from elftools.elf.elffile import ELFFile

class Soc(st.Component):

    def __init__(self, parent, name, parser, chip):
        super(Soc, self).__init__(parent, name)

        #
        # Properties
        #
        #TODO: read properties for the SoC

        #
        # Components
        #

        # Loader
        [args, __] = parser.parse_known_args()

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)
        
        entry = 0
        if binary is not None:
            # Extract entry point when simulating
            with open(args.binary, 'rb') as file_desc:
                elffile = ELFFile(file_desc)
                entry = elffile['e_entry']

        # Debug ROM
        debug_rom = memory.Memory(self, 'debug_rom', size=0x00040000, 
                                  stim_file=self.get_file_path('pulp/chips/pulp/debug_rom.bin'))

        # Memory
        spm = memory.Memory(self, 'spm', size=0x10000000, atomics=True, width_log2=-1)
        dram = memory.Memory(self, 'dram', size=0x80000000, atomics=True, width_log2=-1)
        
        # Last Level Cache (LLC)
        # TODO: change LLC parameters
        llc = cache.Cache(self, 'llc', enabled=True, nb_sets_bits=5, nb_ways_bits=2, line_size_bits=6)
        llc_ctrl = Icache_ctrl(self, 'llc_ctrl')
        
        # Peripherals
        uart = ns16550.Ns16550(self, 'uart', offset_shift=2)
        clint = cpu.clint.Clint(self, 'clint')
        plic = cpu.plic.Plic(self, 'plic', ndev=1)

        # GPIO
        # TODO: change numbers and connect this
        # gpio = gpio_module.Gpio(self, 'gpio', nb_gpio=65, soc_event=33)

        # SoC Registers
        regs = soc_regs.ControlRegs(self, 'control_regs')

        # CVA6 Host
        host = cva6.CVA6(self, 'host', isa="rv64imafdc", boot_addr=entry)

        # System DMA
        idma = IDma(self, 'idma')

        # Narrow 64bits router
        narrow_axi = router.Router(self, 'narrow_axi', bandwidth=8, latency=5)

        #
        # Bindings
        #

        # Main components
        narrow_axi.o_MAP(debug_rom.i_INPUT(), name='debug_rom', base=0x00000000, size=0x00040000)
        narrow_axi.o_MAP(idma.i_INPUT(), name='idma', base=0x01000000, size=0x00010000)
        narrow_axi.o_MAP(clint.i_INPUT(), name='clint', base=0x02040000, size=0x00400000)
        narrow_axi.o_MAP(regs.i_INPUT(), name='control_regs', base=0x03000000, size=0x00001000)
        narrow_axi.o_MAP(llc_ctrl.i_INPUT(), name='llc_ctrl', base=0x03001000, size=0x00001000)
        narrow_axi.o_MAP(uart.i_INPUT(), name='uart', base=0x03002000, size=0x00001000)
        narrow_axi.o_MAP(plic.i_INPUT(), name='plic', base=0x04000000, size=0x08000000)
        narrow_axi.o_MAP(spm.i_INPUT(), name='spm', base=0x10000000, size=0x10000000, latency=5)
        narrow_axi.o_MAP(dram.i_INPUT(), name='dram', base=0x80000000, size=0x80000000, latency=5)
        
        # TODO: LLC bindings quickfix
        if entry == 0x10000000:
            # LLC in SPM mode, bypass cache
            self.bind(host, 'fetch', narrow_axi, 'input')
        else:
            # LLC in iCache mode, use cache
            self.bind(host, 'fetch', llc, 'input')
            
        # Other binds
        self.bind(host, 'data', narrow_axi, 'input')
        self.bind(loader, 'out', narrow_axi, 'input')
        self.bind(loader, 'start', host, 'fetchen')
        self.bind(host, 'time', clint, 'time')
        self.bind(clint, 'sw_irq_0', host, 'msi')
        self.bind(clint, 'timer_irq_0', host, 'mti')
        self.bind(uart, 'irq', plic, 'irq1')
        self.bind(plic, 's_irq_0', host, 'sei')
        self.bind(plic, 'm_irq_0', host, 'mei')
        self.bind(host, 'flush_cache_req', llc, 'flush')
        self.bind(llc, 'flush_ack', host, 'flush_cache_ack')
        self.bind(llc, 'refill', narrow_axi, 'input')
        self.bind(llc_ctrl, 'enable', llc, 'enable')
        self.bind(llc_ctrl, 'flush', llc, 'flush')
        self.bind(llc_ctrl, 'flush', host, 'flush_cache')
        self.bind(llc_ctrl, 'flush_line', llc, 'flush_line')
        self.bind(llc_ctrl, 'flush_line_addr', llc, 'flush_line_addr')
