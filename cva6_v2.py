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
from pulp.cpu.iss.cva6_v2 import Cva6V2
from pulp.cpu.iss.cva6_v2_config import Cva6V2Config
import memory.memory_v3 as memory_v3
from memory.memory_v3 import MemoryV3Config
from vp.clock_domain import Clock_domain
import interco.router_v2 as router_v2
from interco.router_v2 import RouterConfig, RouterMapping, KIND_BANDWIDTH
import devices.uart.ns16550_v2 as ns16550_v2
import cpu.clint_v2
import cpu.plic_v2
import utils.loader.loader_v2 as loader_v2
import gvsoc.systree as st
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
from gvrun.parameter import TargetParameter


# CVA6 virtual board on the io_v2 protocol, same memory map and devices as the cva6 one
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

        # With the legacy gvsoc launcher, configure() is never called and options come
        # from the command line, so they must be resolved now. With gvrun, they come
        # from parameters, the binary being handled in configure() since it can also
        # be set from the build process.
        binary = None
        if os.environ.get('USE_GVRUN') is None and parser is not None:
            parser.add_argument("--isa", dest="isa", type=str, default="rv64imafdc",
                help="RISCV-V ISA string (default: %(default)s)")

            [args, __] = parser.parse_known_args()
            binary = args.binary
            isa = args.isa
        else:
            isa = self.get_parameter('isa')

        mem = memory_v3.Memory(self, 'mem', config=MemoryV3Config(size=0x80000000,
            atomics=True, latency=0))
        rom = memory_v3.Memory(self, 'rom', config=MemoryV3Config(size=0x10000, latency=0,
            stim_file=self.get_file_path('pulp/chips/rv64/rom.bin')))
        uart = ns16550_v2.Ns16550V2(self, 'uart')
        clint = cpu.clint_v2.ClintV2(self, 'clint')
        plic = cpu.plic_v2.PlicV2(self, 'plic', ndev=1)

        # Requests are handled inline, like with the synchronous v1 router
        ico = router_v2.Router(self, 'ico', config=RouterConfig(kind=KIND_BANDWIDTH))

        ico.o_MAP(mem.i_INPUT(), RouterMapping(base=0x80000000, size=0x80000000), name='mem')
        ico.o_MAP(rom.i_INPUT(), RouterMapping(base=0x00001000, size=0x10000), name='rom')
        ico.o_MAP(uart.i_INPUT(), RouterMapping(base=0x10000000, size=0x100), name='uart')
        ico.o_MAP(clint.i_INPUT(), RouterMapping(base=0x2000000, size=0x10000), name='clint')
        ico.o_MAP(plic.i_INPUT(), RouterMapping(base=0xC000000, size=0x1000000), name='plic')
        self.bind(uart, 'irq', plic, 'irq1')

        # HTIF like the CVA6 RTL testharness, so that bare-metal programs exit through tohost.
        # Binaries without a tohost symbol are not polled.
        host = Cva6V2(self, 'host', config=Cva6V2Config(isa=isa, boot_addr=0x1000,
            htif=True))

        loader = loader_v2.ElfLoader(self, 'loader', binary=binary)

        # io_v2 slaves have a single master, so each master has its own router input
        host.o_DATA(ico.i_INPUT(0))
        host.o_FETCH(ico.i_INPUT(1))
        loader.o_OUT(ico.i_INPUT(2))
        self.bind(host, 'time', clint, 'time')
        loader.o_START(host.i_FETCHEN())

        self.bind(clint, 'sw_irq_0', host, 'msi')
        self.bind(clint, 'timer_irq_0', host, 'mti')
        self.bind(plic, 's_irq_0', host, 'sei')
        self.bind(plic, 'm_irq_0', host, 'mei')

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

    def handle_binary(self, binary: str):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)


class Cva6Chip(st.Component):

    def __init__(self, parent, name, parser, options):

        super(Cva6Chip, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        soc = Soc(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    gapy_description="CVA6 virtual board, io_v2"
    model = Cva6Chip
    name = "cva6_v2"

    def __init__(self, parser, options=None, name=None):
        super(Target, self).__init__(parser, options,
            model=Cva6Chip, name=name)
