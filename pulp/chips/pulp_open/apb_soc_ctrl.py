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

import gvsoc.systree as st

class Apb_soc_ctrl(st.Component):

    def __init__(self, parent, name, soc):
        super(Apb_soc_ctrl, self).__init__(parent, name)

        # The model takes no parameters (soc is kept for the callers)
        self.set_component('pulp.chips.pulp.apb_soc_impl')
