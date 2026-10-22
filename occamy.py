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
import gvsoc.systree
from gvsoc.signature import IoV2Beat
import cpu.iss_v2.riscv as iss
import cpu.iss.isa_gen.isa_riscv_gen as isa_riscv_gen
from cpu.iss_v2.riscv_config import RiscvConfig
import memory.memory_v3 as memory_v3
from memory.memory_v3 import MemoryV3Config
from vp.clock_domain import Clock_domain
import interco.router_v2 as router_v2
from interco.router_v2 import RouterConfig, RouterMapping, KIND_BEAT, KIND_UNTIMED
import interco.limiter_v2 as limiter_v2
from interco.limiter_v2 import LimiterConfig
from cpu.clint_v2 import ClintV2
from cpu.plic_v2 import PlicV2
import utils.loader.loader_v2 as loader_v2
from pulp.idma.idma import IDma
from pulp.chips.occamy.quad_cfg import QuadCfg
from pulp.chips.occamy.soc_reg import SocReg
from pulp.chips.occamy.quadrant import Quadrant
import pulp.chips.occamy.occamy_arch
from pulp.snitch.zero_mem_v2 import ZeroMem
import memory.dramsys
from gvrun.parameter import TargetParameter


class Cva6Host(iss.RiscvCommon):
    """iss_v2 core like cpu.iss_v2.riscv.Riscv, with an LSU which can have 4 accesses
    in flight, so that stores are posted like with the CVA6 store buffer."""

    def __init__(self, parent, name, config: RiscvConfig, binaries: list[str]=[]):
        # Own ISA instance, the LSU defines go into it
        isa_instance = isa_riscv_gen.RiscvIsa('occamy_host_' + config.isa,
            config.isa, inc_supervisor=True, inc_user=True)

        modules = {
            'exec': iss.ExecInOrder(scoreboard=True),
            'regfile': iss.Regfile(scoreboard=True),
            'prefetch': iss.PrefetchSingleLine(size=64),
            'lsu': iss.LsuV2(width=isa_instance.word_size // 8, nb_outstanding=4),
            'irq': iss.Irq(),
        }

        # Only a timed core goes on while a store is in flight
        super().__init__(parent, name, config=config, isa=isa_instance, timed=True,
            binaries=binaries, modules=modules)

        self.add_c_flags(['-DCONFIG_GVSOC_ISS_USER_MODE=1'])


class Soc(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, debug_binaries):
        super().__init__(parent, name)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        #
        # Components
        #

        # Bootrom
        rom = memory_v3.Memory(self, 'rom', config=MemoryV3Config(size=arch.bootrom.size,
            latency=0, stim_file=self.get_file_path('pulp/chips/occamy/bootrom.bin')))

        # CVA6, with its stores posted like with the CVA6 store buffer. The offload
        # runtime wakes the clusters up before writing their entry point to the SoC
        # scratch register, the clusters would read it too early if each store waited
        # for its response.
        # TODO binary loader is bypassing this boot addr
        host = Cva6Host(self, 'host', binaries=debug_binaries,
            config=RiscvConfig(isa='rv64imafdc', boot_addr=0x0100_0000, htif=True))

        # System DMA
        idma = IDma(self, 'sys_dma')

        # Narrow 64bits router
        narrow_axi = router_v2.Router(self, 'narrow_axi', config=RouterConfig(
            kind=KIND_BEAT, width=8, max_pending_bursts_per_input=4))

        # Wide 512 bits router
        wide_axi = router_v2.Router(self, 'wide_axi', config=RouterConfig(
            kind=KIND_BEAT, width=64, max_pending_bursts_per_input=4))

        # A beat router cannot bind another one of a different width directly, so the
        # narrow/wide crossings go through untimed bridges (the framework inserts the
        # beat adapters on both sides)
        narrow_to_wide = router_v2.Router(self, 'narrow_to_wide',
            config=RouterConfig(kind=KIND_UNTIMED))
        wide_to_narrow = router_v2.Router(self, 'wide_to_narrow',
            config=RouterConfig(kind=KIND_UNTIMED))

        # Qaudrants
        quadrants = []
        for id in range(0, arch.nb_quadrant):
            quadrants.append(Quadrant(self, f'quadrant_{id}', arch.get_quadrant(id)))

        # Extra component for binary loading
        loader = loader_v2.ElfLoader(self, 'loader', binary=binary)

        # The loader emits 64 KB chunks, while the cluster TCDM wants requests within
        # one 8-byte granule. Split them to the narrow bus width.
        loader_limiter = limiter_v2.Limiter(self, 'loader_limiter',
            config=LimiterConfig(bandwidth=8))

        # Clint
        clint = ClintV2(self, 'clint')

        # Plic
        plic = PlicV2(self, 'plic', ndev=1)

        # Soc control
        soc_reg = SocReg(self, 'soc_reg')

        # Narrow SPM
        spm_narrow = memory_v3.Memory(self, 'narrow_spm', config=MemoryV3Config(
            size=arch.spm_narrow.size, latency=0))

        # Wide SPM
        spm_wide = memory_v3.Memory(self, 'wide_spm', config=MemoryV3Config(
            size=arch.spm_wide.size, latency=0))

        # Quadrant configs
        quad_cfgs = []
        for id in range(0, arch.nb_quadrant):
            quad_cfgs.append(QuadCfg(self, f'quadrant_cfg_{id}'))

        # Wide zero memory
        zero_mem = ZeroMem(self, 'zero_mem', size=arch.wide_zero_mem.size)

        #
        # Bindings
        #

        # Router inputs: the host, then the quadrants, then the loader and the bridges
        narrow_quadrant_input = 1
        narrow_loader_input = narrow_quadrant_input + arch.nb_quadrant
        narrow_bridge_input = narrow_loader_input + 1
        wide_quadrant_input = 1
        wide_bridge_input = wide_quadrant_input + arch.nb_quadrant

        # CVA6
        host.o_DATA(narrow_axi.i_INPUT(0))
        host.o_FETCH(wide_axi.i_INPUT(0))
        host.o_TIME(clint.i_TIME())

        # Quadrant configs
        for id in range(0, arch.nb_quadrant):
            quad_cfgs[id].o_QUADRANT_RESET(quadrants[id].i_RESET())
        for id in range(0, arch.nb_quadrant):
            narrow_axi.o_MAP(quad_cfgs[id].i_INPUT(),
                RouterMapping(base=arch.quad_cfg_base(id), size=arch.quad_cfg.size),
                name=f'quadrant_cfg_{id}')

        # Narrow 64bits router
        for id in range(0, arch.nb_quadrant):
            quadrants[id].o_NARROW_SOC(narrow_axi.i_INPUT(narrow_quadrant_input + id))
            narrow_axi.o_MAP(quadrants[id].i_NARROW_INPUT(),
                RouterMapping(base=arch.quadrant_base(id), size=arch.quadrant.size,
                    remove_base=False), name=f'quadrant_{id}')

        # Wide 512 bits router
        for id in range(0, arch.nb_quadrant):
            quadrants[id].o_WIDE_SOC(wide_axi.i_INPUT(wide_quadrant_input + id))
            wide_axi.o_MAP(quadrants[id].i_WIDE_INPUT(),
                RouterMapping(base=arch.quadrant_base(id), size=arch.quadrant.size,
                    remove_base=False), name=f'quadrant_{id}')

        # Narrow accesses to the wide memories (HBM, ROM, wide SPM, zero memory) and wide
        # accesses to the narrow SPM cross to the other router
        for index, area_name in enumerate(['hbm_0_alias', 'bootrom', 'spm_wide',
                'wide_zero_mem']):
            area = getattr(arch, area_name)
            narrow_axi.o_MAP(narrow_to_wide.i_INPUT(index),
                RouterMapping(base=area.base, size=area.size, remove_base=False),
                name=area_name)
        narrow_to_wide.o_MAP_DEFAULT(wide_axi.i_INPUT(wide_bridge_input), name='wide')

        wide_axi.o_MAP(wide_to_narrow.i_INPUT(0),
            RouterMapping(base=arch.spm_narrow.base, size=arch.spm_narrow.size,
                remove_base=False), name='spm_narrow')
        wide_to_narrow.o_MAP_DEFAULT(narrow_axi.i_INPUT(narrow_bridge_input), name='narrow')

        # HBM
        wide_axi.o_MAP(self.i_HBM(),
            RouterMapping(base=arch.hbm_0_alias.base, size=arch.hbm_0_alias.size,
                latency=100), name='hbm')

        # ROM
        wide_axi.o_MAP(rom.i_INPUT(),
            RouterMapping(base=arch.bootrom.base, size=arch.bootrom.size), name='rom')

        # Soc control
        narrow_axi.o_MAP(soc_reg.i_INPUT(),
            RouterMapping(base=arch.soc_ctrl.base, size=arch.soc_ctrl.size), name='soc_reg')

        # Narrow SPM
        narrow_axi.o_MAP(spm_narrow.i_INPUT(),
            RouterMapping(base=arch.spm_narrow.base, size=arch.spm_narrow.size),
            name='spm_narrow')

        # Wide SPM
        wide_axi.o_MAP(spm_wide.i_INPUT(),
            RouterMapping(base=arch.spm_wide.base, size=arch.spm_wide.size), name='spm_wide')

        # Wide zero memory
        wide_axi.o_MAP(zero_mem.i_INPUT(),
            RouterMapping(base=arch.wide_zero_mem.base, size=arch.wide_zero_mem.size),
            name='zero_mem')

        # Clint
        clint.o_SW_IRQ(0, host.i_IRQ(3))
        clint.o_TIMER_IRQ(0, host.i_IRQ(7))
        narrow_axi.o_MAP(clint.i_INPUT(),
            RouterMapping(base=arch.clint.base, size=arch.clint.size), name='clint')

        # Plic
        plic.o_S_IRQ(0, host.i_IRQ(9))
        plic.o_M_IRQ(0, host.i_IRQ(11))
        narrow_axi.o_MAP(plic.i_INPUT(),
            RouterMapping(base=arch.plic.base, size=arch.plic.size), name='plic')

        # System DMA
        narrow_axi.o_MAP(idma.i_INPUT(),
            RouterMapping(base=arch.sys_idma_cfg.base, size=arch.sys_idma_cfg.size),
            name='sys_dma')

        # Binary loader
        loader.o_OUT(loader_limiter.i_INPUT())
        loader_limiter.o_OUTPUT(narrow_axi.i_INPUT(narrow_loader_input))
        loader.o_START(host.i_FETCHEN())
        loader.o_ENTRY(host.i_ENTRY())

        self.loader = loader
        self.host = host
        self.register_binary_handler(self.handle_binary)

    @override
    def configure(self) -> None:
        # We configure the loader binary now in the configure step since it is coming from
        # a parameter which can be set either from command line or from the build process.
        # The host core also gets it for its HTIF handling and debug symbols.
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)
            self.host.handle_executable(binary)

    def handle_binary(self, binary: str):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)

    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature=IoV2Beat(64))

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature=IoV2Beat(64))



class Occamy(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, debug_binaries):
        super(Occamy, self).__init__(parent, name)

        soc = Soc(self, 'soc', arch.soc, binary, debug_binaries)

        soc.o_HBM(self.i_HBM())

    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature=IoV2Beat(64))

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature=IoV2Beat(64))



class OccamyBoard(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super(OccamyBoard, self).__init__(parent, name, options=options)

        # With the legacy gvsoc launcher, configure() is never called and the binary comes
        # from the command line, so it must be resolved now. With gvrun, it comes from a
        # parameter and is handled in configure() since it can also be set from the build
        # process.
        binary = None
        debug_binaries = []
        if os.environ.get('USE_GVRUN') is None and parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary
            if binary is not None:
                debug_binaries.append(binary)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        arch = pulp.chips.occamy.occamy_arch.OccamyArch(self)

        chip = Occamy(self, 'chip', arch.chip, binary, debug_binaries)

        if arch.hbm.type == 'dramsys':
            # Access size of the default HBM2 memspec
            mem = memory.dramsys.Dramsys(self, 'ddr', version=2, access_size=32)
        else:
            mem = memory_v3.Memory(self, 'mem', config=MemoryV3Config(size=arch.hbm.size,
                atomics=True, latency=0, init=False))

        self.bind(clock, 'out', chip, 'clock')
        self.bind(clock, 'out', mem, 'clock')
        # Signatured binding, so that the beat-to-sync adapter is inserted in front of the
        # memory
        chip.o_HBM(mem.i_INPUT())



class Target(gvsoc.runner.Target):

    gapy_description="Occamy virtual board"
    model = OccamyBoard
    name = "occamy"

    def __init__(self, parser, options=None, name=None):
        super(Target, self).__init__(parser, options,
            model=OccamyBoard, name=name)
