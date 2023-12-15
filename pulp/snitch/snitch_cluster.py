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

import gvsoc.runner
import cpu.iss.riscv as iss
import memory.memory as memory
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from interco.bus_watchpoint import Bus_watchpoint
from pulp.spatz.cluster_registers import Cluster_registers
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc


GAPY_TARGET = True

class Soc(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        nb_cores = 8
        # Snitch core complex
        int_cores = []
        fp_cores = []

        parser.add_argument("--isa", dest="isa", type=str, default="rv32imfdvc",
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

        for core_id in range(0, nb_cores):
            int_cores.append(iss.Snitch(self, f'pe{core_id}', isa=args.isa, core_id=core_id))
            fp_cores.append(iss.Snitch_fp_ss(self, f'fp_ss{core_id}', isa=args.isa, core_id=core_id))

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry=0x1000)


        ico.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x1000000)
        self.bind(ico, 'mem', mem, 'input')

        ico.add_mapping('tcdm', base=0x10000000, remove_offset=0x10000000, size=0x20000)
        self.bind(ico, 'tcdm', tcdm, 'input')

        ico.add_mapping('rom', base=0x00001000, remove_offset=0x00001000, size=0x10000)
        self.bind(ico, 'rom', rom, 'input')


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

        cluster_registers = Cluster_registers(self, 'cluster_registers', boot_addr=entry,
            nb_cores=nb_cores)
        ico.add_mapping('cluster_registers', base=0x00120000, remove_offset=0x00120000, size=0x1000)
        self.bind(ico, 'cluster_registers', cluster_registers, 'input')

        tohost = Bus_watchpoint(self, 'tohost', tohost_addr, fromhost_addr, word_size=32)
        self.bind(tohost, 'output', ico, 'input')

        for core_id in range(0, nb_cores):
            # Sync all core complex by integer cores.
            self.bind(int_cores[core_id], 'barrier_req', cluster_registers, f'barrier_req_{core_id}')
            self.bind(cluster_registers, f'barrier_ack', int_cores[core_id], 'barrier_ack')

        for core_id in range(0, nb_cores):
            # Snitch integer cores
            self.bind(int_cores[core_id], 'data', tohost, 'input')
            self.bind(int_cores[core_id], 'fetch', ico, 'input')
            self.bind(loader, 'start', int_cores[core_id], 'fetchen')
            self.bind(loader, 'entry', int_cores[core_id], 'bootaddr')
            
            # Snitch fp subsystems
            # Pay attention to interactions and bandwidth between subsystem and tohost.
            self.bind(fp_cores[core_id], 'data', tohost, 'input')
            # FP subsystem doesn't fetch instructions from core->ico->memory, but from integer cores acc_req.
            self.bind(loader, 'start', fp_cores[core_id], 'fetchen')
            self.bind(loader, 'entry', fp_cores[core_id], 'bootaddr')
            # Use WireMaster & WireSlave
            self.bind(int_cores[core_id], 'acc_req', fp_cores[core_id], 'acc_req')
            self.bind(fp_cores[core_id], 'acc_rsp', int_cores[core_id], 'acc_rsp')

        self.bind(loader, 'out', ico, 'input')



class SnitchSystem(st.Component):

    def __init__(self, parent, name, parser, options):

        super(SnitchSystem, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=50000000)

        soc = Soc(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=SnitchSystem, description="Snitch virtual board")

