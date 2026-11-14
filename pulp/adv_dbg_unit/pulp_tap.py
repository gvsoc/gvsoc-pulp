#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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

import gvsoc.systree as st
from config_tree import Config, cfg_field


class PulpTapConfig(Config):
    """Configuration of the PULP debug TAP."""

    confreg_instr: int = cfg_field(default=0, desc="JTAG instruction of the configuration register")
    confreg_length: int = cfg_field(default=0, desc="Length in bits of the configuration register")


class Pulp_tap(st.Component):

    def __init__(self, parent, name, confreg_instr=0, confreg_length=0, idcode=0):

        super(Pulp_tap, self).__init__(parent, name, config=PulpTapConfig(
            confreg_instr=confreg_instr, confreg_length=confreg_length))

        self.add_sources(['pulp/adv_dbg_unit/adv_dbg_unit_impl.cpp'])
