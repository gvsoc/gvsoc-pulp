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

import gvsoc.systree as st
import memory.memory as memory
import interco.router as router
import devices.uart.ns16550 as ns16550
import cpu.clint
import cpu.plic
import utils.loader.loader
import pulp.chips.cheshire.cva6.cva6 as cva6
import pulp.chips.cheshire.soc_regs as soc_regs
from pulp.stdout.stdout_v3 import Stdout
from pulp.idma.idma import IDma


class Soc(st.Component):

    def __init__(self, parent, name, parser, chip):
        super(Soc, self).__init__(parent, name)

        #
        # Properties
        #
        #TODO: read properties for the SoC

        #
        # Components
        #

        # Loader
        [args, __] = parser.parse_known_args()

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        # Memory
        dram = memory.Memory(self, 'dram', size=0x10000000, atomics=True, width_log2=-1)
        mem = memory.Memory(self, 'mem', size=0x80000000, atomics=True, width_log2=-1)
        
        # Peripherals
        uart = ns16550.Ns16550(self, 'uart') # FIXME: replace with TI uart
        clint = cpu.clint.Clint(self, 'clint')
        plic = cpu.plic.Plic(self, 'plic', ndev=1)

        # SoC Registers
        regs = soc_regs.ControlRegs(self, 'control_regs')

        # CVA6 Host
        host = cva6.CVA6(self, 'host', isa="rv64imafdc", boot_addr=0x80000000)

        # Standard output
        stdout = Stdout(self, 'stdout')

        # System DMA
        idma = IDma(self, 'sys_dma')

        # Narrow 64bits router
        narrow_axi = router.Router(self, 'narrow_axi', bandwidth=8)

        #
        # Bindings
        #
        
        # Main components
        narrow_axi.o_MAP(mem.i_INPUT(), name='mem', base=0x80000000, size=0x80000000, latency=5)        
        narrow_axi.o_MAP(dram.i_INPUT(), name='dram', base=0xB0000000, size=0x10000000, latency=5)
        narrow_axi.o_MAP(regs.i_INPUT(), name='control_regs', base=0x03000000, size=0x10000000)
        narrow_axi.o_MAP(clint.i_INPUT(), name='clint', base=0x02040000, size=0x0010000)
        narrow_axi.o_MAP(uart.i_INPUT(), name='uart', base=0x03002000, size=0x10000000)
        narrow_axi.o_MAP(idma.i_INPUT(), name='idma', base=0x01000000, size=0x00010000)
        narrow_axi.o_MAP(plic.i_INPUT(), name='plic', base=0x04000000, size=0x01000000)
        # narrow_axi.o_MAP(stdout.i_INPUT(), name='stdout', base=0x03002000, size=0x10000000)
        

        # Other binds
        self.bind(host, 'data', narrow_axi, 'input')
        self.bind(host, 'meminfo', dram, 'meminfo')
        self.bind(host, 'fetch', narrow_axi, 'input')
        self.bind(loader, 'out', narrow_axi, 'input')
        self.bind(loader, 'start', host, 'fetchen')
        self.bind(host, 'time', clint, 'time')
        self.bind(clint, 'sw_irq_0', host, 'msi')
        self.bind(clint, 'timer_irq_0', host, 'mti')
        self.bind(uart, 'irq', plic, 'irq1')
        self.bind(plic, 's_irq_0', host, 'sei')
        self.bind(plic, 'm_irq_0', host, 'mei')
        