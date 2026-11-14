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


class FllConfig(Config):
    """Configuration of the FLL."""

    status_reset: int = cfg_field(default=0x00000000, fmt="hex", desc="Reset value of STATUS")
    conf1_reset: int = cfg_field(default=0x454C05F5, fmt="hex", desc="Reset value of CONF1")
    conf2_reset: int = cfg_field(default=0x02004109, fmt="hex", desc="Reset value of CONF2")
    integrator_reset: int = cfg_field(default=0x00000000, fmt="hex",
        desc="Reset value of INTEGRATOR")


class Fll(st.Component):

    def __init__(self, parent, name):

        super(Fll, self).__init__(parent, name, config=FllConfig())

        self.add_sources(['pulp/fll/fll_v1_impl.cpp'])
