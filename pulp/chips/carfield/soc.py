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
import cpu.iss.riscv as iss
import memory.memory as memory
import interco.router as router
import devices.uart.ns16550 as ns16550
import cpu.clint
import cpu.plic
import utils.loader.loader
from interco.bus_watchpoint import Bus_watchpoint
from elftools.elf.elffile import *

class Soc(st.Component):

    def __init__(self, parent, name, parser, config_file, chip):
        super(Soc, self).__init__(parent, name)

        #
        # Properties
        #

        #self.add_properties(self.load_property_file(config_file))


        #
        # Components
        #
        
        # Loader

        [args, __] = parser.parse_known_args()
        
        binary = None
        if parser is not None:
            [args, __] = parser.parse_known_args()
            binary = args.binary

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        # Standard rv64
        mem = memory.Memory(self, 'mem', size=0x80000000, atomics=True)
        #FIXME: fix path below
        rom = memory.Memory(self, 'rom', size=0x10000, stim_file=self.get_file_path('pulp/chips/rv64/rom.bin'))
        uart = ns16550.Ns16550(self, 'uart')
        clint = cpu.clint.Clint(self, 'clint')
        plic = cpu.plic.Plic(self, 'plic', ndev=1)

        ico = router.Router(self, 'ico')

        ico.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x80000000)
        self.bind(ico, 'mem', mem, 'input')

        ico.add_mapping('rom', base=0x00001000, remove_offset=0x00001000, size=0x10000)
        self.bind(ico, 'rom', rom, 'input')

        ico.add_mapping('uart', base=0x10000000, remove_offset=0x10000000, size=0x100)
        self.bind(ico, 'uart', uart, 'input')

        ico.add_mapping('clint', base=0x2000000, remove_offset=0x2000000, size=0x10000)
        self.bind(ico, 'clint', clint, 'input')

        ico.add_mapping('plic', base=0xC000000, remove_offset=0xC000000, size=0x1000000)
        self.bind(ico, 'plic', plic, 'input')
        self.bind(uart, 'irq', plic, 'irq1')

        host = iss.Riscv(self, 'host', isa='rv64imafdc', boot_addr=0x1000, timed=True)

        # RISCV bus watchpoint
        tohost_addr = 0
        fromhost_addr = 0
        if binary is not None:
            with open(binary, 'rb') as file:
                elffile = ELFFile(file)
                for section in elffile.iter_sections():
                    if isinstance(section, SymbolTableSection):
                        for symbol in section.iter_symbols():
                            if symbol.name == 'tohost':
                                tohost_addr = symbol.entry['st_value']
                            if symbol.name == 'fromhost':
                                fromhost_addr = symbol.entry['st_value']

        tohost = Bus_watchpoint(self, 'tohost', tohost_addr, fromhost_addr, word_size=64, args=args.args)
        

        #
        # Bindings
        #
        
        self.bind(host, 'data', tohost, 'input')
        self.bind(tohost, 'output', ico, 'input')

        self.bind(host, 'meminfo', mem, 'meminfo')
        self.bind(host, 'fetch', ico, 'input')
        self.bind(host, 'time', clint, 'time')
        self.bind(loader, 'out', ico, 'input')
        self.bind(loader, 'start', host, 'fetchen')
        # self.bind(loader, 'entry', host, 'bootaddr')

        self.bind(clint, 'sw_irq_0', host, 'msi')
        self.bind(clint, 'timer_irq_0', host, 'mti')
        self.bind(plic, 's_irq_0', host, 'sei')
        self.bind(plic, 'm_irq_0', host, 'mei')