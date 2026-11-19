#
# Copyright (C) 2ETH Zurich and University of Bologna
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

import gvsoc.systree
from config_tree import Config, cfg_field


class Cva6ControlRegsConfig(Config):
    dram_end: int = cfg_field(default=0, fmt="hex", desc="End address of the DRAM, returned by the DRAM end register")


class ControlRegs(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str, dram_end=0x0, latency: int=0):
        super().__init__(parent, name, config=Cva6ControlRegsConfig(dram_end=dram_end))

        self.add_sources(['pulp/cva6/control_regs.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
