# SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Marco Paci (marco.paci@chips.it)

from typing import Annotated

import gvsoc.systree
from config_tree import Config, cfg_field
from gvrun.runtime import Runtime


class Cv32e40pTestbenchStrapsConfig(Config):

    # Runtime field, so that each test sets it without rebuilding the platform.
    mtvec_addr: Annotated[int, Runtime] = cfg_field(default=0, fmt="hex", dump=True, desc=(
        "mtvec base at boot (RTL mtvec_addr_i)"
    ))


class Cv32e40pTestbenchStraps(gvsoc.systree.Component):
    """Static inputs of the core driven by the test, at the end of the reset."""

    def __init__(self, parent, name, config: Cv32e40pTestbenchStrapsConfig):
        super().__init__(parent, name, config=config)
        self.add_sources(['pulp/cv32e40p/cv32e40p_testbench_straps.cpp'])

    def o_MTVEC_ADDR(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('mtvec_addr', itf, signature='wire<uint32_t>')
