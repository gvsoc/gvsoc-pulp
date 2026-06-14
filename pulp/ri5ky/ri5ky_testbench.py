#
# Copyright (C) 2026 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#

try:
    from typing import override
except ImportError:
    from typing_extensions import override

from vp.clock_domain import Clock_domain
from gvsoc.systree import Component
from gvrun.parameter import TargetParameter

from pulp.ri5ky.ri5ky_testbench_config import (
    Ri5kyTestbenchConfig, Ri5kyTestbenchBoardConfig,
)
from pulp.ri5ky.ri5ky_mmio import Ri5kyMmio
from utils.loader.loader_v2 import ElfLoader
from ips.gap.cpu.ri5ky import Ri5ky
from memory.memory_v3 import Memory
from interco.router_v2 import Router
from pulp.ri5ky.ri5ky_async_mem import Ri5kyAsyncMem


class Ri5kyTestbench(Component):
    """GVSoC counterpart of hw/ri5ky_gwt/gv_tb/riscv_soc.sv.

    Same memory map (mem at 0x0, MMIO at 0x1000_0000) so the same firmware
    runs on both simulators.
    """

    def __init__(self, parent: Component, name: str, config: Ri5kyTestbenchConfig):
        super().__init__(parent, name, config=config)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        mem      = Memory       ( self, 'mem'     , config=config.mem      )
        # Asynchronous slow memory: answers IO_REQ_GRANTED and replies
        # `latency` cycles later, mirroring RTL slow_mem's rvalid-after-L and
        # engaging p.elw's clock-gated park/wake path (a real event unit is
        # always an asynchronous responder).
        slow_mem = Ri5kyAsyncMem ( self, 'slow_mem', config=config.slow_mem )
        mmio     = Ri5kyMmio    ( self, 'mmio'                              )
        ico      = Router       ( self, 'ico'     , config=config.router   )
        core     = Ri5ky        ( self, 'core'    , config=config.core     )
        loader   = ElfLoader    ( self, 'loader'                            )

        ico.o_MAP        ( mem.i_INPUT()      , mapping=config.mem_mapping      )
        ico.o_MAP        ( slow_mem.i_INPUT() , mapping=config.slow_mem_mapping )
        ico.o_MAP        ( mmio.i_INPUT()     , mapping=config.mmio_mapping     )

        # Three independent masters, one router input port each.
        loader.o_OUT     ( ico.i_INPUT(0)   )
        loader.o_START   ( core.i_FETCHEN() )
        loader.o_ENTRY   ( core.i_ENTRY()   )

        core.o_FETCH     ( ico.i_INPUT(1) )
        core.o_DATA      ( ico.i_INPUT(2) )

        self.loader: ElfLoader = loader
        self.register_binary_handler(self.handle_binary)

    @override
    def configure(self) -> None:
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)

    def handle_binary(self, binary: str):
        self.set_parameter('binary', binary)


class Ri5kyTestbenchBoard(Component):

    def __init__(self, parent: Component, name: str, config: Ri5kyTestbenchBoardConfig):

        super().__init__(parent, name, config=config)

        self.set_target_name('ri5ky.testbench')

        clock = Clock_domain  ( self, 'clock', frequency=config.frequency )
        soc   = Ri5kyTestbench( self, 'soc',   config.soc                 )

        clock.o_CLOCK ( soc.i_CLOCK() )
