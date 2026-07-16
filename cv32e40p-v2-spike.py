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

# CV32E40P iss_v2 bring-up target.
#
# The iss_v2 LSU (LsuV2) drives the fetch/data ports with the io_v2
# protocol, so the whole platform lives on the io_v2 plane: router_v2,
# memory_v3 and loader_v2, following the Ri5ky testbench layout
# (pulp/ri5ky/ri5ky_testbench.py). The MMIO peripheral is reused as-is
# from that testbench: putchar @ +0x0, exit @ +0x4.
#
# Integer-only for the bring-up: rv32imc + the CoreV (XPULP v2) subset.

import vp.clock_domain
import gvsoc.systree
import gvsoc.runner
from gvrun.parameter import TargetParameter
from config_tree import Config, cfg_field
from memory.memory_v3 import Memory, MemoryV3Config
from interco.router_v2 import Router, RouterConfig, RouterMapping
from utils.loader.loader_v2 import ElfLoader
from pulp.ri5ky.ri5ky_mmio import Ri5kyMmio
from pulp.cpu.iss.cv32e40p_v2 import Cv32e40p as Cv32e40pV2Core, Cv32e40pConfig


class Cv32e40pSpikeConfig(Config):
    """Configuration for the CV32E40P iss_v2 bring-up SoC.

    Minimal layout:
      - mem  at 0x0000_0000 (4 MB), entry point 0x80 as in the RTL testbench
      - MMIO at 0x1000_0000 (4 KB): putchar @ +0, exit @ +4
    """

    mem_base: int = cfg_field(default=0x0000_0000, fmt="hex", dump=True, desc=(
        "Base address of the main memory"
    ))

    mem_size: int = cfg_field(default=0x40_0000, fmt="hex", dump=True, desc=(
        "Size of the main memory"
    ))

    mmio_base: int = cfg_field(default=0x1000_0000, fmt="hex", dump=True, desc=(
        "Base address of the MMIO peripheral"
    ))

    mmio_size: int = cfg_field(default=0x1000, fmt="hex", dump=True, desc=(
        "Size of the MMIO peripheral window"
    ))

    boot_addr: int = cfg_field(default=0x80, fmt="hex", dump=True, desc=(
        "Boot address (matches RTL BOOT_ADDR)"
    ))

    core: Cv32e40pConfig = cfg_field(init=False, desc=(
        "CV32E40P core configuration"
    ))

    mem: MemoryV3Config = cfg_field(init=False, desc=(
        "Backing memory configuration"
    ))

    router: RouterConfig = cfg_field(init=False, desc=(
        "Router configuration"
    ))

    mem_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the main memory"
    ))

    mmio_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the MMIO peripheral"
    ))

    def __post_init__(self):
        super().__post_init__()
        # No compressed for now: with C enabled the generated decode table
        # references the rvf c.flwsp/c.fswsp handlers even when they are
        # inactive (no F), and without an FPU module nothing declares them.
        self.core = Cv32e40pConfig(isa='rv32im', boot_addr=self.boot_addr)
        self.mem = MemoryV3Config('mem', size=self.mem_size, atomics=False, latency=1)
        self.router = RouterConfig(kind='bandwidth')
        self.mem_mapping = RouterMapping(name='mem_mapping',
                                         base=self.mem_base, size=self.mem_size)
        self.mmio_mapping = RouterMapping(name='mmio_mapping',
                                          base=self.mmio_base, size=self.mmio_size)


class Cv32e40pSpikeSoc(gvsoc.systree.Component):

    def __init__(self, parent, name, config: Cv32e40pSpikeConfig, binary):
        super().__init__(parent, name, config=config)

        mem    = Memory        ( self, 'mem'   , config=config.mem    )
        mmio   = Ri5kyMmio     ( self, 'mmio'                         )
        ico    = Router        ( self, 'ico'   , config=config.router )
        core   = Cv32e40pV2Core( self, 'core'  , config=config.core   )
        loader = ElfLoader     ( self, 'loader', binary=binary        )

        ico.o_MAP ( mem.i_INPUT() , mapping=config.mem_mapping  )
        ico.o_MAP ( mmio.i_INPUT(), mapping=config.mmio_mapping )

        # Three independent masters, one router input port each.
        loader.o_OUT   ( ico.i_INPUT(0)   )
        loader.o_START ( core.i_FETCHEN() )
        loader.o_ENTRY ( core.i_ENTRY()   )

        core.o_FETCH ( ico.i_INPUT(1) )
        core.o_DATA  ( ico.i_INPUT(2) )


class Cv32e40p(gvsoc.systree.Component):

    def __init__(self, parent, name=None):
        super().__init__(parent, name)

        binary = TargetParameter(
            self, name='binary', value=None, description='ELF binary to simulate'
        ).get_value()

        config = Cv32e40pSpikeConfig('soc')

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=50000000)
        soc = Cv32e40pSpikeSoc(self, 'soc', config, binary)
        clock.o_CLOCK(soc.i_CLOCK())


class Target(gvsoc.runner.Target):

    description = "CV32E40P iss_v2 bring-up"
    model = Cv32e40p
    name = "cv32e40p-v2-spike"
