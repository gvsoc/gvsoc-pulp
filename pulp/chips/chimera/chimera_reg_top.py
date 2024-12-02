#
# Copyright (C) 2020 GreenWaves Technologies
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

class chimera_reg_top(gvsoc.systree.Component):

    def __init__(self, parent, name):
        super(chimera_reg_top, self).__init__(parent, name)

        self.add_sources(['pulp/chips/chimera/chimera_reg_top.cpp'])
    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        """Returns the input port.

        Incoming requests to be handled by the memory should be sent to this port.\n
        It instantiates a port of type vp::IoSlave.\n

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')