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

try:
    from typing import override  # Python 3.12+
except ImportError:
    from typing_extensions import override  # Python 3.10–3.11

import os

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
from gvrun.parameter import TargetParameter


class Soc(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        # With the legacy gvsoc launcher, configure() is never called and the binary comes
        # from the command line, so it must be resolved now. With gvrun, it comes from a
        # parameter and is handled in configure() since it can also be set from the build
        # process.
        binary = None
        if os.environ.get('USE_GVRUN') is None and parser is not None:
            [args, __] = parser.parse_known_args()
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

        self.loader = loader
        self.register_binary_handler(self.handle_binary)

    @override
    def configure(self) -> None:
        # We configure the loader binary now in the configure step since it is coming from
        # a parameter which can be set either from command line or from the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)

    def handle_binary(self, binary: str):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)


class AraChip(st.Component):

    def __init__(self, parent, name, parser, options):

        super(AraChip, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        soc = Soc(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    gapy_description="Ara virtual board"
    model = AraChip
    name = "ara"

    def __init__(self, parser, options=None, name=None):
        super(Target, self).__init__(parser, options,
            model=AraChip, name=name)
