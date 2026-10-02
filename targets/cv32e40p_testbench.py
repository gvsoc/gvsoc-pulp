# SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Marco Paci (marco.paci@chips.it)

# CV32E40P testbench. The RTL parameters are fields of the SoC configuration:
#   gvrun --target cv32e40p_testbench:config.soc/corev_pulp=true:config.soc/fpu=true ...

from gvsoc.systree import Component
import gvsoc.runner

from pulp.cv32e40p.cv32e40p_testbench_config import Cv32e40pTestbenchBoardConfig
from pulp.cv32e40p.cv32e40p_testbench import Cv32e40pTestbenchBoard


class Target(gvsoc.runner.Target):

    gapy_description: str = "CV32E40P testbench"
    model: type[Component] = Cv32e40pTestbenchBoard
    name: str = ""
    config: Cv32e40pTestbenchBoardConfig = Cv32e40pTestbenchBoardConfig("cv32e40p_testbench")
