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
import gvsoc.systree as st
from interco.bus_watchpoint import Bus_watchpoint
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
from pulp.redmule.redmule import RedMule
from pulp.stdout.stdout_v3 import Stdout


class RedmuleTestbench(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        imemory = memory.Memory(self, 'imem', size=0x10000, atomics=True)
        dmemory = memory.Memory(self, 'dmem', size=0x10000, atomics=True)
        stackmemory = memory.Memory(self, 'stackmem', size=0x10000, atomics=True)

        ico = router.Router(self, 'ico')
        host = iss.Riscv(self, 'host', isa='rv32imc', timed=True)
        redmule = RedMule(self, 'redmule')
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)
        stdout = Stdout(self, 'stdout')

        ico.o_MAP(stdout.i_INPUT(), base=0x80000004, size=0x4)
        ico.o_MAP(redmule.i_INPUT(), base=0x00100000, size=0x10000)
        ico.o_MAP(imemory.i_INPUT(), base=0x1c000000, size=0x10000)
        ico.o_MAP(dmemory.i_INPUT(), base=0x1c010000, size=0x10000)
        ico.o_MAP(dmemory.i_INPUT(), name='dmem_alias', base=0x00010000, size=0x10000)
        ico.o_MAP(stackmemory.i_INPUT(), base=0x1c040000, size=0x10000)

        loader.o_OUT(ico.i_INPUT())
        loader.o_START(host.i_FETCHEN())
        loader.o_ENTRY(host.i_ENTRY())
        host.o_DATA(ico.i_INPUT())
        host.o_FETCH(ico.i_INPUT())
        redmule.o_OUT(dmemory.i_INPUT())


class RedmuleTestbenchWrapper(st.Component):

    def __init__(self, parent, name, parser, options):

        super(RedmuleTestbenchWrapper, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        soc = RedmuleTestbench(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    gapy_description="Redmule testbench"

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=RedmuleTestbenchWrapper)
