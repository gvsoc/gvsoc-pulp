#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
#                    University of Bologna
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

# Python wrapper for the CV32E40P virtual exit device.
# Terminates GVSOC simulation when the test program writes to 0x2000_0004.

import gvsoc.systree as st


class Cv32e40pExitDevice(st.Component):
    """
    CV32E40P virtual peripheral status flags device.

    Memory map (base-relative):
      +0x00  VP status flags   (test_passed / test_failed — sink)
      +0x04  exit_valid write  → quits GVSOC simulation with exit_value = wdata
      +0x08  sig_start_addr    (sink)
      +0x0C  sig_end_addr      (sink)
      +0x10  sig_write trigger → quits GVSOC simulation with exit_value = 0
    """

    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.set_component('pulp.cv32e40p_exit.cv32e40p_exit_device')

    def i_INPUT(self) -> st.SlaveItf:
        return st.SlaveItf(self, 'input', signature='io')
