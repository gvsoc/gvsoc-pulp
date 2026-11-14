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


class SocEuConfig(Config):
    """Configuration of the SoC event unit."""

    ref_clock_event: int = cfg_field(default=-1, desc=(
        "Event triggered on each reference clock edge, -1 for none"))
    nb_fc_events: int = cfg_field(default=0, desc=(
        "Number of events triggered for the FC at reset"))
    first_fc_event: int = cfg_field(default=0, desc=(
        "First event triggered for the FC at reset"))


class Soc_eu(st.Component):

    def __init__(self, parent, name, ref_clock_event=-1, irq_redirect=[]):

        super(Soc_eu, self).__init__(parent, name, config=SocEuConfig(
            ref_clock_event=ref_clock_event))

        self.add_sources(['pulp/soc_eu/soc_eu_v2_impl.cpp'])
