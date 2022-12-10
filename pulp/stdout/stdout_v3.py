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

class Stdout(st.Component):

    def __init__(self, parent, name):

        super(Stdout, self).__init__(parent, name)

        self.set_component('pulp.stdout.stdout_v3_impl')
        self.add_properties({
            'max_cluster': 1024,
            'max_core_per_cluster': 16
        })
