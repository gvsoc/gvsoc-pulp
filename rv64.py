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

import cpu.iss_v2.riscv as iss
from cpu.iss_v2.riscv_config import RiscvConfig
from memory.memory_v3 import Memory, MemoryV3Config
from vp.clock_domain import Clock_domain
from interco.router_v2 import Router, RouterConfig, RouterMapping, KIND_UNTIMED
from devices.uart.ns16550_v2 import Ns16550V2
from cpu.clint_v2 import ClintV2
from cpu.plic_v2 import PlicV2
from utils.loader.loader_v2 import ElfLoader
import gvsoc.systree as st
import gvsoc.runner as gvsoc
from gvrun.parameter import TargetParameter


class Soc(st.Component):
    """RV64 virtual board: a generic 64-bit core with 2 GB of DRAM, the spike boot ROM, a
    NS16550 UART, a CLINT and a PLIC, on io_v2. Bare-metal programs and the proxy kernel
    exit and do their system calls through HTIF, handled by the core."""

    def __init__(self, parent, name, parser, timed=True):
        super().__init__(parent, name)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )
        _ = TargetParameter(
            self, name='isa', value='rv64imafdc', description='RISC-V ISA string',
            cast=str
        )
        _ = TargetParameter(
            self, name='args', value=None,
            description='Space-separated application arguments (passed to main)',
            cast=str
        )

        # With the legacy gvsoc launcher, configure() is never called and options come
        # from the command line, so they must be resolved now. With gvrun, they come
        # from parameters, the binary being handled in configure() since it can also
        # be set from the build process.
        binary = None
        htif_args = []
        if os.environ.get('USE_GVRUN') is None and parser is not None:
            parser.add_argument("--isa", dest="isa", type=str, default="rv64imafdc",
                help="RISCV-V ISA string (default: %(default)s)")

            parser.add_argument("--arg", dest="args", action="append",
                help="Specify application argument (passed to main)")

            [args, __] = parser.parse_known_args()
            binary = args.binary
            isa = args.isa
            if args.args is not None:
                htif_args = args.args
        else:
            isa = self.get_parameter('isa')

        # Zero-latency interconnect and memories, the core timing being the only one
        mem = Memory(self, 'mem', config=MemoryV3Config(size=0x80000000, atomics=True,
            latency=0))
        rom = Memory(self, 'rom', config=MemoryV3Config(size=0x10000, latency=0,
            stim_file=self.get_file_path('pulp/chips/rv64/rom.bin')))
        uart = Ns16550V2(self, 'uart')
        clint = ClintV2(self, 'clint')
        plic = PlicV2(self, 'plic', ndev=1)

        ico = Router(self, 'ico', config=RouterConfig(kind=KIND_UNTIMED))

        ico.o_MAP(mem.i_INPUT(), mapping=RouterMapping(base=0x80000000, size=0x80000000))
        ico.o_MAP(rom.i_INPUT(), mapping=RouterMapping(base=0x00001000, size=0x10000))
        ico.o_MAP(uart.i_INPUT(), mapping=RouterMapping(base=0x10000000, size=0x100))
        ico.o_MAP(clint.i_INPUT(), mapping=RouterMapping(base=0x2000000, size=0x10000))
        ico.o_MAP(plic.i_INPUT(), mapping=RouterMapping(base=0xC000000, size=0x1000000))
        uart.o_IRQ(plic.i_IRQ(0))

        # Binaries without a tohost symbol are not polled
        core_config = RiscvConfig(isa=isa, boot_addr=0x1000, htif=True, mmu=True, pmp=True)
        if not timed:
            # Without timing, the core reads and writes the DRAM array directly, which is
            # much faster than going through the interconnect
            core_config.memory_start = 0x80000000
            core_config.memory_size = 0x80000000
        host = iss.Riscv(self, 'host', timed=timed, binaries=[binary] if binary else [],
            config=core_config)
        host.add_property('args', htif_args)

        loader = ElfLoader(self, 'loader', binary=binary)

        # io_v2 slaves take a single master, each master has its own router input
        loader.o_OUT(ico.i_INPUT(0))
        host.o_FETCH(ico.i_INPUT(1))
        host.o_DATA(ico.i_INPUT(2))
        host.o_TIME(clint.i_TIME())
        if not timed:
            host.o_MEMINFO(mem.i_MEMINFO())
        loader.o_START(host.i_FETCHEN())

        clint.o_SW_IRQ(0, host.i_IRQ(3))
        clint.o_TIMER_IRQ(0, host.i_IRQ(7))
        plic.o_S_IRQ(0, host.i_IRQ(9))
        plic.o_M_IRQ(0, host.i_IRQ(11))

        self.loader = loader
        self.host = host
        self.register_binary_handler(self.handle_binary)

    @override
    def configure(self) -> None:
        # We configure the loader binary now in the configure step since it is coming from
        # a parameter which can be set either from command line or from the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)
            # The binary does not come as a tree executable, so the core cannot find
            # tohost/fromhost by itself
            self.host.handle_htif(binary)

        args = self.get_parameter('args')
        if args is not None:
            self.host.add_property('args', args.split())

    def handle_binary(self, binary: str):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)


class Rv64(st.Component):

    # False to simulate the core without timing
    timed = True

    def __init__(self, parent, name, parser, options):

        super(Rv64, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        soc = Soc(self, 'soc', parser, timed=self.timed)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    gapy_description="RV64 virtual board"
    model = Rv64
    name = "rv64"

    def __init__(self, parser, options=None, name=None):
        super(Target, self).__init__(parser, options,
            model=Rv64, name=name)
