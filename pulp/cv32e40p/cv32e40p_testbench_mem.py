# SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Marco Paci (marco.paci@chips.it)

from typing import Annotated

import gvsoc.systree
import gvsoc.signature
from config_tree import Config, cfg_field
from gvrun.runtime import Runtime


class Cv32e40pTestbenchMemConfig(Config):

    # Runtime field, so that each co-simulation run sets it without rebuilding the platform.
    stop_on_exit: Annotated[bool, Runtime] = cfg_field(default=True, dump=True, desc=(
        "Stop the simulation when the program reports its end. False keeps the core running "
        "after the report, as the core of the UVM testbench does (co-simulation)"
    ))

    print_stdout: bool = cfg_field(default=True, dump=True, desc=(
        "Write what the program prints to the standard output. False in co-simulation, where "
        "the UVM testbench prints it"
    ))


class Cv32e40pTestbenchMem(gvsoc.systree.Component):
    """Memory of the CV32E40P testbench outside the main memory and the debug memory, with the
    virtual peripherals of the core-v-verif UVM testbench at their register addresses. It reads
    0 where it was never written. It is mapped as the catch-all route, with the absolute address
    (remove_base=False).
    """

    def __init__(self, parent, name, config: Cv32e40pTestbenchMemConfig):
        super().__init__(parent, name, config=config)
        self.add_sources(['pulp/cv32e40p/cv32e40p_testbench_mem.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=gvsoc.signature.IoV2Sync())
