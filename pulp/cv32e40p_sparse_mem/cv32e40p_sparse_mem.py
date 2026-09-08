#
# Copyright (C) 2026 Fondazione Chips-it
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
# Authors: Marco Paci, Fondazione Chips-it (marco.paci@chips.it)
#

# Python wrapper for the CV32E40P background sparse memory.
# Mapped as the interconnect's default route: serves every access no explicit
# device claims (never-written bytes read 0, writes persist), matching the UVM
# testbench sparse memory model.

import gvsoc.systree as st


class Cv32e40pSparseMem(st.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.set_component('pulp.cv32e40p_sparse_mem.cv32e40p_sparse_mem')

    def i_INPUT(self) -> st.SlaveItf:
        return st.SlaveItf(self, 'input', signature='io')
