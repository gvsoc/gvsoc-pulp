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
from pulp.chips.occamy.quadrant import Quadrant


GAPY_TARGET = True

class Soc(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        [args, otherArgs] = parser.parse_known_args()
        binary = args.binary
        binaries = []
        if binary is not None:
            binaries.append(binary)

        mem = memory.Memory(self, 'mem', size=0x80000000, atomics=True)

        # uart = ns16550.Ns16550(self, 'uart')

        clint = cpu.clint.Clint(self, 'clint')

        plic = cpu.plic.Plic(self, 'plic', ndev=1)

        idma = IDma(self, 'sys_dma')

        soc_reg = SocReg(self, 'soc_reg')

        ico = router.Router(self, 'ico')

        rom = memory.Memory(self, 'rom', size=0x10000, stim_file=self.get_file_path('pulp/chips/occamy/bootrom.bin'))

        host = iss.Riscv(self, 'host', isa="rv64imafdc", boot_addr=0x1000, timed=False,
            binaries=binaries, htif=True)

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)


        nb_quadrant = 5
        for id in range(0, nb_quadrant):
            quad_cfg = QuadCfg(self, f'quadrant_cfg_{id}')
            cluster = Quadrant(self, f'quadrant_{id}')

            qaudrant_cfg_size = 0x10000
            quadrant_cfg_base = 0x0b000000 + id * qaudrant_cfg_size

            ico.o_MAP ( quad_cfg.i_INPUT (), base=quadrant_cfg_base, size=qaudrant_cfg_size, rm_base=True  )
            quad_cfg.o_QUADRANT_RESET(cluster.i_RESET())

            qaudrant_size = 0x100000
            quadrant_base = 0x10000000 + id * qaudrant_size

            cluster.o_SOC(ico.i_INPUT())
            ico.o_MAP ( cluster.i_INPUT    (), base=quadrant_base, size=qaudrant_size, rm_base=False )



        ico.o_MAP ( mem.i_INPUT        (), base=0x80000000, size=0x80000000, rm_base=True  )
        # ico.o_MAP ( uart.i_INPUT       (), base=0x10000000, size=0x00000100, rm_base=True  )
        ico.o_MAP ( clint.i_INPUT      (), base=0x04000000, size=0x00100000, rm_base=True  )
        ico.o_MAP ( plic.i_INPUT       (), base=0x0C000000, size=0x01000000, rm_base=True  )
        ico.o_MAP ( rom.i_INPUT        (), base=0x01000000, size=0x00010000, rm_base=True  )
        ico.o_MAP ( idma.i_INPUT       (), base=0x11000000, size=0x00010000, rm_base=True  )
        ico.o_MAP ( soc_reg.i_INPUT    (), base=0x02000000, size=0x00001000, rm_base=True  )

        # uart.o_IRQ ( plic.i_IRQ (device=0))

        host.o_DATA(ico.i_INPUT())
        host.o_MEMINFO(mem.i_INPUT())
        host.o_FETCH(ico.i_INPUT())
        host.o_TIME(clint.i_TIME())

        loader.o_OUT(ico.i_INPUT())
        loader.o_START(host.i_FETCHEN())
        loader.o_ENTRY(host.i_ENTRY())

        clint.o_SW_IRQ(core=0, itf=host.i_IRQ(3))
        clint.o_TIMER_IRQ(core=0, itf=host.i_IRQ(7))

        plic.o_S_IRQ(core=0, itf=host.i_IRQ(9))
        plic.o_M_IRQ(core=0, itf=host.i_IRQ(11))


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

