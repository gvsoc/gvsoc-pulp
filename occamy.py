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
import devices.uart.ns16550 as ns16550
import cpu.clint
import cpu.plic
import utils.loader.loader
from pulp.idma.idma import IDma
import gvsoc.systree as st
from interco.bus_watchpoint import Bus_watchpoint
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
from pulp.chips.occamy.quad_cfg import QuadCfg
from pulp.chips.occamy.soc_reg import SocReg
from pulp.snitch.snitch_cluster.snitch_cluster_functional import SnitchCluster


GAPY_TARGET = True

class Soc(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        [args, otherArgs] = parser.parse_known_args()
        binary = args.binary

        mem = memory.Memory(self, 'mem', size=0x80000000, atomics=True)
        uart = ns16550.Ns16550(self, 'uart')
        clint = cpu.clint.Clint(self, 'clint')
        plic = cpu.plic.Plic(self, 'plic', ndev=1)
        idma = IDma(self, 'sys_dma')
        quad_cfg_0 = QuadCfg(self, 'quad_cfg_0')
        cluster = SnitchCluster(self, 'cluster_0')
        soc_reg = SocReg(self, 'soc_reg')

        ico = router.Router(self, 'ico')

        rom = memory.Memory(self, 'rom', size=0x10000, stim_file=self.get_file_path('pulp/chips/occamy/bootrom.bin'))

        ico.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x80000000)
        self.bind(ico, 'mem', mem, 'input')

        ico.add_mapping('uart', base=0x10000000, remove_offset=0x10000000, size=0x100)
        self.bind(ico, 'uart', uart, 'input')

        # ico.add_mapping('clint', base=0x2000000, remove_offset=0x2000000, size=0x10000)
        # self.bind(ico, 'clint', clint, 'input')

        ico.add_mapping('plic', base=0xC000000, remove_offset=0xC000000, size=0x1000000)
        self.bind(ico, 'plic', plic, 'input')
        self.bind(uart, 'irq', plic, 'irq1')

        ico.add_mapping('rom', base=0x01000000, remove_offset=0x01000000, size=0x10000)
        self.bind(ico, 'rom', rom, 'input')

        ico.o_MAP(quad_cfg_0.i_INPUT(), name='quad_cfg_0', base=0x0b000000, size=0x10000, rm_base=True)
        ico.o_MAP(idma.i_INPUT(), name='sys_dma', base=0x11000000, size=0x10000, rm_base=True)
        ico.o_MAP(cluster.i_INPUT(), name='cluster_0', base=0x10000000, size=0x40000, rm_base=False)
        ico.o_MAP(soc_reg.i_INPUT(), name='soc_reg', base=0x02000000, size=0x1000, rm_base=True)
        cluster.o_SOC(ico.i_INPUT())

        host = iss.Riscv(self, 'host', isa="rv64imafdc", boot_addr=0x1000, timed=False)

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        quad_cfg_0.o_QUADRANT_RESET(cluster.i_RESET())

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

        tohost = Bus_watchpoint(self, 'tohost', tohost_addr, fromhost_addr, word_size=64)
        self.bind(host, 'data', tohost, 'input')
        self.bind(tohost, 'output', ico, 'input')

        self.bind(host, 'meminfo', mem, 'meminfo')
        self.bind(host, 'fetch', ico, 'input')
        self.bind(host, 'time', clint, 'time')
        self.bind(loader, 'out', ico, 'input')
        self.bind(loader, 'start', host, 'fetchen')
        self.bind(loader, 'entry', host, 'bootaddr')

        self.bind(clint, 'sw_irq_0', host, 'msi')
        self.bind(clint, 'timer_irq_0', host, 'mti')
        self.bind(plic, 's_irq_0', host, 'sei')
        self.bind(plic, 'm_irq_0', host, 'mei')


class Occamy(st.Component):

    def __init__(self, parent, name, parser, options):

        super(Occamy, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        soc = Soc(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=Occamy, description="Occamy virtual board")

