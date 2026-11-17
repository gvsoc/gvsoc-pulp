#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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
from gvsoc.signature import IoV2Sync


class Cva6tbConsole(gvsoc.systree.Component):
    """Simulation console of the CVA6 standalone testbench (test/hw/cva6tb_sim_console.sv).

    A byte written at offset 0 is appended to a line buffer, which is printed on stdout at
    each newline. Reads return 0.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str):
        super().__init__(parent, name)

        self.add_sources(['pulp/cva6tb/cva6tb_console.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Sync())
