# Copyright (C) 2025 Fondazione Chips-IT

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree

class SnitchSpatzRegs(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str):

        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia_v2/spatz/snitch_spatz_regs.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
    
    def o_CLK_EN(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('clock_en', itf, signature='wire<bool>')

    def o_START(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('start_irq', itf, signature='wire<bool>')