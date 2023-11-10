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

import gv.gvsoc_runner
import cpu.iss.iss as iss
import memory.memory as memory
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
import gsystree as st
from interco.bus_watchpoint import Bus_watchpoint
from pulp.spatz.cluster_registers import Cluster_registers
from elftools.elf.elffile import *
import gv.gvsoc_runner as gvsoc


GAPY_TARGET = True

class Soc(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        parser.add_argument("--isa", dest="isa", type=str, default="rv64imfdvc",
            help="RISCV-V ISA string (default: %(default)s)")

        [args, __] = parser.parse_known_args()

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        rom = memory.Memory(self, 'rom', size=0x10000, stim_file=self.get_file_path('pulp/chips/spatz/rom.bin'))

        mem = memory.Memory(self, 'mem', size=0x1000000)

        tcdm = memory.Memory(self, 'tcdm', size=0x40000)

        ico = router.Router(self, 'ico')

        ico.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x1000000)
        self.bind(ico, 'mem', mem, 'input')

        ico.add_mapping('tcdm', base=0x100000, remove_offset=0x100000, size=0x40000)
        self.bind(ico, 'tcdm', tcdm, 'input')

        ico.add_mapping('rom', base=0x00001000, remove_offset=0x00001000, size=0x10000)
        self.bind(ico, 'rom', rom, 'input')

        host = iss.Iss(self, 'host', vp_component='pulp.cpu.iss.iss_snitch', isa=args.isa, scoreboard=True)

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry=0x1000)


        # RISCV bus watchpoint
        tohost_addr = 0
        fromhost_addr = 0
        entry = 0
        if binary is not None:
            with open(binary, 'rb') as file:
                elffile = ELFFile(file)
                entry = elffile['e_entry']
                for section in elffile.iter_sections():
                    if isinstance(section, SymbolTableSection):
                        for symbol in section.iter_symbols():
                            if symbol.name == 'tohost':
                                tohost_addr = symbol.entry['st_value']
                            if symbol.name == 'fromhost':
                                fromhost_addr = symbol.entry['st_value']

        cluster_registers = Cluster_registers(self, 'cluster_registers', boot_addr=entry)
        ico.add_mapping('cluster_registers', base=0x00120000, remove_offset=0x00120000, size=0x1000)
        self.bind(ico, 'cluster_registers', cluster_registers, 'input')

        tohost = Bus_watchpoint(self, 'tohost', tohost_addr, fromhost_addr, word_size=32)
        self.bind(host, 'data', tohost, 'input')
        self.bind(host, 'vlsu_0', tohost, 'input')
        self.bind(host, 'vlsu_1', tohost, 'input')
        self.bind(host, 'vlsu_2', tohost, 'input')
        self.bind(host, 'vlsu_3', tohost, 'input')
        self.bind(tohost, 'output', ico, 'input')

        self.bind(host, 'fetch', ico, 'input')
        self.bind(loader, 'out', ico, 'input')
        self.bind(loader, 'start', host, 'fetchen')
        self.bind(loader, 'entry', host, 'bootaddr')



class Spatz(st.Component):

    def __init__(self, parent, name, parser, options):

        super(Spatz, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=50000000)

        soc = Soc(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=Spatz, description="Spatz virtual board")

