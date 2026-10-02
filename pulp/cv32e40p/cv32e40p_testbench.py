# SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Marco Paci (marco.paci@chips.it)

try:
    from typing import override
except ImportError:
    from typing_extensions import override

from vp.clock_domain import Clock_domain
from gvsoc.systree import Component, SlaveItf
from gvrun.parameter import TargetParameter

from pulp.cv32e40p.cv32e40p_testbench_config import (
    Cv32e40pTestbenchConfig, Cv32e40pTestbenchBoardConfig,
)
from pulp.cv32e40p.cv32e40p_testbench_mem import Cv32e40pTestbenchMem
from pulp.cv32e40p.cv32e40p_testbench_straps import Cv32e40pTestbenchStraps
from pulp.cv32e40p.cv32e40p import Cv32e40p
from utils.loader.loader_v2 import ElfLoader
from memory.memory_v3 import Memory
from interco.router_v2 import Router


class Cv32e40pTestbench(Component):
    """GVSoC counterpart of the memory of the core-v-verif CV32E40P UVM testbench
    and of its virtual peripherals. It has the same memory map, so the test
    programs of core-v-verif run unchanged. There is no source of interrupts,
    debug requests or random stalls, so the programs that need them do not end
    here. The parameters binary and mtvec_addr (RTL mtvec_addr_i, the
    +mtvec_addr plusarg of the testbench) are given per test.
    """

    def __init__(self, parent: Component, name: str, config: Cv32e40pTestbenchConfig):
        super().__init__(parent, name, config=config)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        config.straps.mtvec_addr = TargetParameter(
            self, name='mtvec_addr', value=0, cast=int,
            description='mtvec base at boot (RTL mtvec_addr_i)'
        ).get_value()

        mem       = Memory                  ( self, 'mem'      , config=config.mem       )
        debug_mem = Memory                  ( self, 'debug_mem', config=config.debug_mem )
        tb_mem    = Cv32e40pTestbenchMem    ( self, 'tb_mem'   , config=config.tb_mem    )
        ico       = Router                  ( self, 'ico'      , config=config.router    )
        core      = Cv32e40p                ( self, 'core'     , config=config.core      )
        straps    = Cv32e40pTestbenchStraps ( self, 'straps'   , config=config.straps    )
        loader    = ElfLoader               ( self, 'loader'                             )

        ico.o_MAP ( mem.i_INPUT()      , mapping=config.mem_mapping       )
        ico.o_MAP ( debug_mem.i_INPUT(), mapping=config.debug_mem_mapping )
        ico.o_MAP ( tb_mem.i_INPUT()   , mapping=config.tb_mem_mapping    )

        # o_ENTRY is not bound, because the core boots at boot_addr like the RTL.
        loader.o_OUT   ( ico.i_INPUT(0)   )
        loader.o_START ( core.i_FETCHEN() )

        core.o_FETCH ( ico.i_INPUT(1) )
        core.o_DATA  ( ico.i_INPUT(2) )

        straps.o_MTVEC_ADDR ( SlaveItf(core, itf_name='mtvec_addr', signature='wire<uint32_t>') )

        self.loader: ElfLoader = loader
        self.register_binary_handler(self.handle_binary)

    @override
    def configure(self) -> None:
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)

    def handle_binary(self, binary: str):
        self.set_parameter('binary', binary)


class Cv32e40pTestbenchBoard(Component):

    def __init__(self, parent: Component, name: str, config: Cv32e40pTestbenchBoardConfig):

        super().__init__(parent, name, config=config)

        self.set_target_name('cv32e40p.testbench')

        clock = Clock_domain      ( self, 'clock', frequency=config.frequency )
        soc   = Cv32e40pTestbench ( self, 'soc'  , config.soc                 )

        clock.o_CLOCK ( soc.i_CLOCK() )
