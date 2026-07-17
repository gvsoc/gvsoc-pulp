#
# Copyright (C) 2026 Fondazione Chips-it
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

#
# Authors: Marco Paci, Fondazione Chips-it (marco.paci@chips.it)
#

# CV32E40P iss_v2 standalone platform for UVM co-simulation, shared by the
# cv32e40p-v2-standalone* targets (one target name per core configuration).
#
# Same memory map as the io-plane co-sim platform (cv32e40p-standalone.py),
# rebuilt on the io_v2 plane the iss_v2 LSU requires:
#   0x00000000  4MB   Main RAM        (entry point 0x00000080)
#   0x10000000  256B  Virtual STDOUT  (write-only sink)
#   0x15000000  256B  Virtual TIMER   (write-only sink)
#   0x1A110800  4KB   Debug ROM       (linker script `dbg` region)
#   0x20000000  256B  Virtual EXIT    (terminates the simulation)
#   everywhere else   background sparse memory (catch-all route: reads 0
#                     until written, writes persist - same as the testbench)

import vp.clock_domain
import gvsoc.systree
from gvrun.parameter import TargetParameter
from config_tree import Config, cfg_field
from memory.memory_v3 import Memory, MemoryV3Config
from interco.router_v2 import Router, RouterConfig, RouterMapping
from utils.loader.loader_v2 import ElfLoader
from pulp.cv32e40p_exit.cv32e40p_exit_device_v2 import Cv32e40pExitDeviceV2
from pulp.cv32e40p_sparse_mem.cv32e40p_sparse_mem_v2 import Cv32e40pSparseMemV2
from pulp.cpu.iss.cv32e40p_v2 import Cv32e40p as Cv32e40pV2Core, Cv32e40pConfig


class Cv32e40pStandaloneConfig(Config):
    """Configuration for the CV32E40P iss_v2 co-simulation SoC."""

    boot_addr: int = cfg_field(default=0x80, fmt="hex", dump=True, desc=(
        "Boot address (matches RTL BOOT_ADDR)"
    ))

    fpu: int = cfg_field(default=0, dump=True, desc=(
        "FPU configuration (F extension, FP register file)"
    ))

    zfinx: int = cfg_field(default=0, dump=True, desc=(
        "ZFINX configuration (FP operations on the integer register file)"
    ))

    core: Cv32e40pConfig = cfg_field(init=False, desc=(
        "CV32E40P core configuration"
    ))

    mem: MemoryV3Config = cfg_field(init=False, desc=(
        "Main RAM configuration"
    ))

    stdout: MemoryV3Config = cfg_field(init=False, desc=(
        "Virtual STDOUT sink configuration"
    ))

    timer: MemoryV3Config = cfg_field(init=False, desc=(
        "Virtual TIMER sink configuration"
    ))

    debug_rom: MemoryV3Config = cfg_field(init=False, desc=(
        "Debug ROM configuration"
    ))

    router: RouterConfig = cfg_field(init=False, desc=(
        "Router configuration"
    ))

    mem_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Main RAM address range"
    ))

    stdout_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Virtual STDOUT address range"
    ))

    timer_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Virtual TIMER address range"
    ))

    debug_rom_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Debug ROM address range"
    ))

    exit_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Virtual EXIT device address range"
    ))

    background_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Background sparse memory catch-all route"
    ))

    def __post_init__(self):
        super().__post_init__()
        # ZFINX needs the F opcodes in the decoder (routed to the integer
        # register file); compressed FP rows are disabled by the core recipe.
        isa = 'rv32imfc' if (self.fpu or self.zfinx) else 'rv32imc'
        self.core = Cv32e40pConfig(isa=isa, boot_addr=self.boot_addr)
        # init=False: never-written bytes must read 0 (testbench memory
        # contract), not the 0x57 poison pattern of the default init=True.
        self.mem       = MemoryV3Config('mem', size=0x0040_0000, atomics=False, latency=1,
                                        init=False)
        self.stdout    = MemoryV3Config('stdout', size=0x100, atomics=False, latency=1,
                                        init=False)
        self.timer     = MemoryV3Config('timer', size=0x100, atomics=False, latency=1,
                                        init=False)
        self.debug_rom = MemoryV3Config('debug_rom', size=0x1000, atomics=False, latency=1,
                                        init=False)
        self.router = RouterConfig(kind='bandwidth')
        self.mem_mapping       = RouterMapping(name='mem_mapping',
                                               base=0x0000_0000, size=0x0040_0000)
        self.stdout_mapping    = RouterMapping(name='stdout_mapping',
                                               base=0x1000_0000, size=0x100)
        self.timer_mapping     = RouterMapping(name='timer_mapping',
                                               base=0x1500_0000, size=0x100)
        self.debug_rom_mapping = RouterMapping(name='debug_rom_mapping',
                                               base=0x1A11_0800, size=0x1000)
        self.exit_mapping      = RouterMapping(name='exit_mapping',
                                               base=0x2000_0000, size=0x100)
        # Catch-all (size=0): absolute addresses forwarded so the sparse
        # store is indexed like the testbench's.
        self.background_mapping = RouterMapping(name='background_mapping',
                                                base=0x0000_0000, size=0,
                                                remove_base=False)


class Cv32e40pStandaloneSoc(gvsoc.systree.Component):

    def __init__(self, parent, name, config: Cv32e40pStandaloneConfig, binary):
        super().__init__(parent, name, config=config)

        mem    = Memory              ( self, 'mem'      , config=config.mem       )
        stdout = Memory              ( self, 'stdout'   , config=config.stdout    )
        timer  = Memory              ( self, 'timer'    , config=config.timer     )
        dbgrom = Memory              ( self, 'debug_rom', config=config.debug_rom )
        exit_d = Cv32e40pExitDeviceV2( self, 'exit'                               )
        bg_mem = Cv32e40pSparseMemV2 ( self, 'background_mem'                     )
        ico    = Router              ( self, 'ico'      , config=config.router    )
        core   = Cv32e40pV2Core      ( self, 'core'     , config=config.core      ,
                                       fpu=bool(config.fpu), zfinx=bool(config.zfinx) )
        loader = ElfLoader           ( self, 'loader'   , binary=binary           )

        ico.o_MAP ( mem.i_INPUT()   , mapping=config.mem_mapping        )
        ico.o_MAP ( stdout.i_INPUT(), mapping=config.stdout_mapping     )
        ico.o_MAP ( timer.i_INPUT() , mapping=config.timer_mapping      )
        ico.o_MAP ( dbgrom.i_INPUT(), mapping=config.debug_rom_mapping  )
        ico.o_MAP ( exit_d.i_INPUT(), mapping=config.exit_mapping       )
        ico.o_MAP ( bg_mem.i_INPUT(), mapping=config.background_mapping )

        # Three independent masters, one router input port each.
        # o_ENTRY is deliberately NOT bound: like the RTL, the core boots at
        # the fixed BOOT_ADDR (config boot_addr), not at the ELF entry; the
        # entry sync would also rewrite mtvec after a co-sim CSR injection.
        loader.o_OUT   ( ico.i_INPUT(0)   )
        loader.o_START ( core.i_FETCHEN() )

        core.o_FETCH ( ico.i_INPUT(1) )
        core.o_DATA  ( ico.i_INPUT(2) )


class Cv32e40pStandaloneTop(gvsoc.systree.Component):

    def __init__(self, parent, name=None, fpu: int=0, zfinx: int=0):
        super().__init__(parent, name)

        binary = TargetParameter(
            self, name='binary', value=None, description='ELF binary to simulate'
        ).get_value()

        config = Cv32e40pStandaloneConfig('soc', fpu=fpu, zfinx=zfinx)

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=50000000)
        soc = Cv32e40pStandaloneSoc(self, 'soc', config, binary)
        clock.o_CLOCK(soc.i_CLOCK())
