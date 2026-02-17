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

#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#

from gvsoc.systree import Component
import gvsoc.runner

from pulp.snitch.snitch_testbench_config import SnitchTestbenchBoardConfig
from pulp.snitch.snitch_testbench import SnitchTestbenchBoard


class Target(gvsoc.runner.Target):

    gapy_description: str = "Snitch testbench"
    model: type[Component] = SnitchTestbenchBoard
    name: str = ""
    config: SnitchTestbenchBoardConfig = SnitchTestbenchBoardConfig()
