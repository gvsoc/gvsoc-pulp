#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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


class EocRegistersConfig(Config):
    """Configuration of the Flooccamy end-of-computation registers."""

    eoc_entry: int = cfg_field(default=0, fmt="hex", dump=True, desc="Address polled for the end of computation")
    interval: int = cfg_field(default=10000, dump=True, desc="Polling interval, in cycles")


class EoC_Registers(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, eoc_entry: int, interval: int=10000):

        super().__init__(parent, name, config=EocRegistersConfig(
            eoc_entry=eoc_entry, interval=interval))

        self.add_sources(['pulp/chips/flooccamy/eoc_registers.cpp'])

    def o_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('output', itf, signature='io')
