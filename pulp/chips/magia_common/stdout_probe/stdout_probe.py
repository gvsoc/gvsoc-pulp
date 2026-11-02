#
# Copyright (C) 2026 Fondazione Chips-IT
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

# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree
from gvsoc.signature import IoV2Beat


class StdoutProbe(gvsoc.systree.Component):
    """Print monitor of the MAGIA testbench on the narrow port of a tile
    towards the NoC (magia_vip.sv, print_monitor on the tile observe port).

    Every request goes through unchanged and in the same cycle: the probe
    only watches. A write to ``addr`` is a character of the tile's stdout,
    printed when a line is complete. The write itself goes on to its slave
    (the L2), which answers it like any other write, as in the RTL.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, width: int, addr: int):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia_common/stdout_probe/stdout_probe.cpp'])
        self.add_properties({
            'addr': addr,
        })
        self.width = width

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Beat(self.width))

    def o_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('output', itf, signature=IoV2Beat(self.width))
