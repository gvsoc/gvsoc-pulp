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
from interco.bus_watchpoint import Bus_watchpoint
from elftools.elf.elffile import *
import gvsoc.runner
import gvsoc.systree
from pulp.chips.occamy.quad_cfg import QuadCfg
from pulp.chips.occamy.soc_reg import SocReg
from pulp.chips.occamy.quadrant import Quadrant
import pulp.chips.occamy.occamy_arch

GAPY_TARGET = True






class Soc(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, debug_binaries):
        super().__init__(parent, name)

        #
        # Components
        #

        # CVA6
        # TODO binary loader is bypassing this boot addr
        host = iss.Riscv(self, 'host', isa="rv64imafdc", boot_addr=0x0100_0000, timed=False,
            binaries=debug_binaries, htif=True)

        # System DMA
        idma = IDma(self, 'sys_dma')

        # Main interconnect
        ico = router.Router(self, 'ico')

        # Qaudrants
        quadrants = []
        for id in range(0, arch.nb_quadrant):
            quadrants.append(Quadrant(self, f'quadrant_{id}', arch.get_quadrant(id)))

        # Extra component for binary loading
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        # Uart, to be added back for linux
        # uart = ns16550.Ns16550(self, 'uart')

        # Clint
        clint = cpu.clint.Clint(self, 'clint')

        # Plic
        plic = cpu.plic.Plic(self, 'plic', ndev=1)

        # Soc control
        soc_reg = SocReg(self, 'soc_reg')

        # Bootrom
        rom = memory.Memory(self, 'rom', size=arch.bootrom.size,
            stim_file=self.get_file_path('pulp/chips/occamy/bootrom.bin'))

        # Quadrant configs
        quad_cfgs = []
        for id in range(0, arch.nb_quadrant):
            quad_cfgs.append(QuadCfg(self, f'quadrant_cfg_{id}'))

        #
        # Bindings
        #

        # CVA6
        host.o_DATA(ico.i_INPUT())
        host.o_MEMINFO(self.i_HBM())
        host.o_FETCH(ico.i_INPUT())
        host.o_TIME(clint.i_TIME())

        # Quadrant configs
        for id in range(0, arch.nb_quadrant):
            quad_cfgs[id].o_QUADRANT_RESET(quadrants[id].i_RESET())

        # Main interconnect
        for id in range(0, arch.nb_quadrant):
            quadrants[id].o_SOC(ico.i_INPUT())
            ico.o_MAP ( quadrants[id].i_INPUT    (), base=arch.quadrant_base(id), size=arch.quadrant.size, rm_base=False )

        for id in range(0, arch.nb_quadrant):
            ico.o_MAP ( quad_cfgs[id].i_INPUT (), base=arch.quad_cfg_base(id), size=arch.quad_cfg.size, rm_base=True  )

        ico.o_MAP ( self.i_HBM      (), base=arch.hbm_0_alias.base, size=arch.hbm_0_alias.size, rm_base=True  )
        # ico.o_MAP ( uart.i_INPUT    (), base=arch.uart.base, size=arch.uart.size, rm_base=True  )
        ico.o_MAP ( clint.i_INPUT   (), base=arch.clint.base, size=arch.clint.size, rm_base=True  )
        ico.o_MAP ( plic.i_INPUT    (), base=arch.plic.base, size=arch.plic.size, rm_base=True  )
        ico.o_MAP ( rom.i_INPUT     (), base=arch.bootrom.base, size=arch.bootrom.size, rm_base=True  )
        ico.o_MAP ( idma.i_INPUT    (), base=arch.sys_idma_cfg.base, size=arch.sys_idma_cfg.size, rm_base=True  )
        ico.o_MAP ( soc_reg.i_INPUT (), base=arch.soc_ctrl.base, size=arch.soc_ctrl.size, rm_base=True  )

        # Clint
        clint.o_SW_IRQ(core=0, itf=host.i_IRQ(3))
        clint.o_TIMER_IRQ(core=0, itf=host.i_IRQ(7))

        # Plic
        plic.o_S_IRQ(core=0, itf=host.i_IRQ(9))
        plic.o_M_IRQ(core=0, itf=host.i_IRQ(11))

        # Uart
        # uart.o_IRQ ( plic.i_IRQ (device=0))

        # Binary loader
        loader.o_OUT(ico.i_INPUT())
        loader.o_START(host.i_FETCHEN())
        loader.o_ENTRY(host.i_ENTRY())

    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature='io')

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature='io')



class Occamy(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, debug_binaries):
        super(Occamy, self).__init__(parent, name)

        soc = Soc(self, 'soc', arch.soc, binary, debug_binaries)

        soc.o_HBM(self.i_HBM())

    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature='io')



class OccamyBoard(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super(OccamyBoard, self).__init__(parent, name, options=options)

        [args, otherArgs] = parser.parse_known_args()
        debug_binaries = []
        if args.binary is not None:
            debug_binaries.append(args.binary)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        arch = pulp.chips.occamy.occamy_arch.OccamyArch(self)

        chip = Occamy(self, 'chip', arch.chip, args.binary, debug_binaries)

        mem = memory.Memory(self, 'mem', size=arch.hbm.size, atomics=True)

        self.bind(clock, 'out', chip, 'clock')
        self.bind(clock, 'out', mem, 'clock')
        self.bind(chip, 'hbm', mem, 'input')



class Target(gvsoc.runner.Target):

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=OccamyBoard, description="Occamy virtual board")

