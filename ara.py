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
import pulp.cva6.cva6
import memory.memory as memory
import pulp.cva6.control_regs
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
from pulp.stdout.stdout_v3 import Stdout


class Soc(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        [args, __] = parser.parse_known_args()

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        dram = memory.Memory(self, 'dram', size=0x10000000, atomics=True, width_log2=-1)

        mem = memory.Memory(self, 'mem', size=0x80000000, atomics=True, width_log2=-1)
        regs = pulp.cva6.control_regs.ControlRegs(self, 'control_regs', dram_end=0xc0000000)

        stdout = Stdout(self, 'stdout')

        ico = router.Router(self, 'ico')

        ico.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x80000000, latency=5)
        self.bind(ico, 'mem', mem, 'input')

        ico.o_MAP(stdout.i_INPUT(), name='stdout', base=0xC0000000, size=0x10000000)
        ico.o_MAP(dram.i_INPUT(), name='dram', base=0xB0000000, size=0x10000000, latency=5)
        ico.o_MAP(regs.i_INPUT(), name='control_regs', base=0xD0000000, size=0x10000000)

        host = pulp.cva6.cva6.CVA6(self, 'host', isa='rv64imafdvc', boot_addr=0x80000000,
            has_vector=True, vlen=4096)

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        self.bind(host, 'data', ico, 'input')
        self.bind(host, 'fetch', ico, 'input')
        self.bind(loader, 'out', ico, 'input')
        self.bind(loader, 'start', host, 'fetchen')

        host.o_VLSU(ico.i_INPUT())


class AraChip(st.Component):

    def __init__(self, parent, name, parser, options):

        super(AraChip, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        soc = Soc(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    gapy_description="Ara virtual board"

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=AraChip)
