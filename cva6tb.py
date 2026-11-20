#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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

"""CVA6 standalone testbench board.

The SoC of the pulp-platform/cva6 standalone testbench (test/hw/cva6tb_soc.sv, pulp-v3),
around the io_v2 CVA6 core of the cva6_v2 board, so that the same ELF runs on both:

    0x0204_0000  CLINT (mtime at the 1 MHz RTC of a 1 GHz core)
    0x0C00_0000  PLIC
    0x1000_0000  platform control registers (boot mode, end of computation, IRQ lines, marker)
    0x1000_1000  simulation console
    0x2000_0000  SPM, 1 MB
    0x4000_0000  DRAM uncached alias, 256 MB
    0x8000_0000  DRAM, 256 MB

The RTL boots through a boot ROM that jumps to the DRAM; this board starts the core at the
DRAM directly. The RTC timer and the CLIC are not modelled.
"""

try:
    from typing import override  # Python 3.12+
except ImportError:
    from typing_extensions import override  # Python 3.10–3.11

import gvsoc.runner as gvsoc
import gvsoc.systree as st
from pulp.cpu.iss.cva6_v2 import Cva6V2
from pulp.cpu.iss.cva6_v2_config import Cva6V2Config
from pulp.cva6tb.cva6tb_regs import Cva6tbRegs
from pulp.cva6tb.cva6tb_console import Cva6tbConsole
import memory.memory_v3 as memory_v3
from memory.memory_v3 import MemoryV3Config
from vp.clock_domain import Clock_domain
import interco.router_v2 as router_v2
from interco.router_v2 import RouterConfig, RouterMapping, KIND_BANDWIDTH
import cpu.clint_v2
import cpu.plic_v2
import utils.loader.loader_v2 as loader_v2
from gvrun.parameter import TargetParameter


DRAM_BASE = 0x80000000
DRAM_ALIAS_BASE = 0x40000000
DRAM_SIZE = 0x10000000
CORE_FREQUENCY = 1000000000
RTC_FREQUENCY = 1000000


class Soc(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )
        _ = TargetParameter(
            self, name='isa', value='rv64imafdc', description='RISC-V ISA string',
            cast=str
        )

        isa = self.get_parameter('isa')

        dram = memory_v3.Memory(self, 'dram', config=MemoryV3Config(size=DRAM_SIZE,
            atomics=True, latency=0))
        spm = memory_v3.Memory(self, 'spm', config=MemoryV3Config(size=0x100000,
            atomics=True, latency=0))
        regs = Cva6tbRegs(self, 'regs')
        console = Cva6tbConsole(self, 'console')
        clint = cpu.clint_v2.ClintV2(self, 'clint', frequency=RTC_FREQUENCY)
        plic = cpu.plic_v2.PlicV2(self, 'plic', ndev=1)

        # Requests are handled inline, like with the synchronous v1 router
        ico = router_v2.Router(self, 'ico', config=RouterConfig(kind=KIND_BANDWIDTH))
        # The DRAM has one input: its cached and uncached windows reach it through this
        dram_ico = router_v2.Router(self, 'dram_ico', config=RouterConfig(kind=KIND_BANDWIDTH))

        ico.o_MAP(clint.i_INPUT(), RouterMapping(base=0x02040000, size=0x40000), name='clint')
        ico.o_MAP(plic.i_INPUT(), RouterMapping(base=0x0C000000, size=0x4000000), name='plic')
        ico.o_MAP(regs.i_INPUT(), RouterMapping(base=0x10000000, size=0x1000), name='regs')
        ico.o_MAP(console.i_INPUT(), RouterMapping(base=0x10001000, size=0x1000),
            name='console')
        ico.o_MAP(spm.i_INPUT(), RouterMapping(base=0x20000000, size=0x100000), name='spm')
        ico.o_MAP(dram_ico.i_INPUT(0), RouterMapping(base=DRAM_BASE, size=DRAM_SIZE,
            remove_base=True), name='dram')
        ico.o_MAP(dram_ico.i_INPUT(1), RouterMapping(base=DRAM_ALIAS_BASE, size=DRAM_SIZE,
            remove_base=True), name='dram_alias')
        dram_ico.o_MAP(dram.i_INPUT(), RouterMapping(base=0, size=DRAM_SIZE), name='dram')

        host = Cva6V2(self, 'host', config=Cva6V2Config(isa=isa, boot_addr=DRAM_BASE,
            htif=False))

        loader = loader_v2.ElfLoader(self, 'loader', binary=None)

        # io_v2 slaves have a single master, so each master has its own router input
        host.o_DATA(ico.i_INPUT(0))
        host.o_FETCH(ico.i_INPUT(1))
        loader.o_OUT(ico.i_INPUT(2))
        self.bind(host, 'time', clint, 'time')
        loader.o_START(host.i_FETCHEN())

        self.bind(clint, 'sw_irq_0', host, 'msi')
        self.bind(clint, 'timer_irq_0', host, 'mti')
        # The testbench ORs these lines with the PLIC ones; no PLIC source is used here, so
        # the register lines drive the core alone
        self.bind(regs, 'irq_m', host, 'mei')
        self.bind(regs, 'irq_s', host, 'sei')

        self.loader = loader
        self.register_binary_handler(self.handle_binary)

    @override
    def configure(self) -> None:
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)

    def handle_binary(self, binary: str):
        self.set_parameter('binary', binary)


class Cva6tbChip(st.Component):

    def __init__(self, parent, name, parser, options):

        super().__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=CORE_FREQUENCY)

        soc = Soc(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):
    # Every component is configured from the compiled platform tree
    json_config = False


    gapy_description = "CVA6 standalone testbench board (pulp-platform/cva6 test/)"
    model = Cva6tbChip
    name = "cva6tb"

    def __init__(self, parser, options=None, name=None):
        super().__init__(parser, options, model=Cva6tbChip, name=name)
