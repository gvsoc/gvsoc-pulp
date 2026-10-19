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

import gvsoc.runner
from pulp.cpu.iss.cva6_v2 import Cva6V2
from pulp.cpu.iss.cva6_config import Cva6Config
import memory.memory_v3 as memory_v3
from memory.memory_v3 import MemoryV3Config
import pulp.cva6.control_regs_v2
from vp.clock_domain import Clock_domain
import interco.router_v2 as router_v2
from interco.router_v2 import RouterConfig, RouterMapping, KIND_BANDWIDTH
import utils.loader.loader_v2 as loader_v2
import gvsoc.systree as st
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
from pulp.stdout.stdout_v3_v2 import StdoutV2
from gvrun.parameter import TargetParameter


class Soc(st.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        dram = memory_v3.Memory(self, 'dram', config=MemoryV3Config(size=0x10000000,
            atomics=True, latency=0))

        mem = memory_v3.Memory(self, 'mem', config=MemoryV3Config(size=0x80000000,
            atomics=True, latency=0))
        regs = pulp.cva6.control_regs_v2.ControlRegsV2(self, 'control_regs',
            dram_end=0xc0000000)

        stdout = StdoutV2(self, 'stdout')

        # Requests are handled inline, with the mapping latency added to them
        ico = router_v2.Router(self, 'ico', config=RouterConfig(kind=KIND_BANDWIDTH))

        ico.o_MAP(mem.i_INPUT(), RouterMapping(base=0x80000000, size=0x80000000, latency=5),
            name='mem')
        ico.o_MAP(stdout.i_INPUT(), RouterMapping(base=0xC0000000, size=0x10000000),
            name='stdout')
        ico.o_MAP(dram.i_INPUT(), RouterMapping(base=0xB0000000, size=0x10000000, latency=5),
            name='dram')
        ico.o_MAP(regs.i_INPUT(), RouterMapping(base=0xD0000000, size=0x10000000),
            name='control_regs')

        config = Cva6Config(fetch_enable=False, htif=True, nb_lanes=4,
            lane_width=8, vlen=4096)
        host = Cva6V2(self, 'host', config=config)

        loader = loader_v2.ElfLoader(self, 'loader')
        loader.o_START(host.i_FETCHEN())
        loader.o_ENTRY(host.i_ENTRY())

        # io_v2 slaves have a single master, so each master has its own router input
        host.o_DATA(ico.i_INPUT(0))
        host.o_FETCH(ico.i_INPUT(1))
        loader.o_OUT(ico.i_INPUT(2))
        host.o_VLSU(ico.i_INPUT(3))

        self.loader = loader
        self.register_binary_handler(self.handle_binary)

    @override
    def configure(self) -> None:
        # We configure the loader binary now int he configure steps since it is coming from
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

        soc = Soc(self, 'soc')

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    gapy_description="Ara virtual board"
    model = AraChip
    name = "ara"

    def __init__(self, parser, options=None, name=None):
        super(Target, self).__init__(parser, options,
            model=AraChip, name=name)
