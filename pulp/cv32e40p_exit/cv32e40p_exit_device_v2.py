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

# CV32E40P virtual exit device on the io_v2 plane: same register map as the
# io-plane sibling (UVM virtual peripheral at 0x20000000).

import gvsoc.systree
import gvsoc.signature


class Cv32e40pExitDeviceV2(gvsoc.systree.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.add_sources(['pulp/cv32e40p_exit/cv32e40p_exit_device_v2.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=gvsoc.signature.IoV2Sync())
