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
import gv.gvsoc_runner as gvsoc

GAPY_TARGET = True

class Soc(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        # TODO, once supported, defaut isa should be rv32imafdc
        parser.add_argument("--isa", dest="isa", type=str, default="rv32imfdc",
            help="RISCV-V ISA string (default: %(default)s)")

        [args, __] = parser.parse_known_args()

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        mem = memory.Memory(self, 'mem', size=0x1000000)

        ico = router.Router(self, 'ico')

        ico.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x1000000)
        self.bind(ico, 'mem', mem, 'input')

        host = iss.Iss(self, 'host', vp_component='pulp.cpu.iss.iss_rv32', isa=args.isa,
            riscv_exceptions=True)

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        # RISCV bus watchpoint
        tohost = Bus_watchpoint(self, 'tohost', 0x80001000, 0x80001000, word_size=64)

        self.bind(host, 'fetch', ico, 'input')
        self.bind(host, 'data', tohost, 'input')
        self.bind(tohost, 'output', ico, 'input')
        self.bind(loader, 'out', ico, 'input')
        self.bind(loader, 'start', host, 'fetchen')
        self.bind(loader, 'entry', host, 'bootaddr')


class Rv32(st.Component):

    def __init__(self, parent, name, parser, options):

        super(Rv32, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=50000000)

        soc = Soc(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=Rv32, description="RV32 virtual board")

