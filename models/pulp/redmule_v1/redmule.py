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

import gsystree as st

class Redmule(st.Component):

    def __init__(self, parent, name, nb_masters=1, nb_rows=12, nb_cols=4, nb_pipes_per_fma=3):

        super(Redmule, self).__init__(parent, name)

        self.set_component('pulp.redmule.redmule_v1_impl')

        self.add_properties({
            'nb_masters': nb_masters,
            'nb_rows': nb_rows,
            'nb_cols': nb_cols,
            'nb_pipes_per_fma': nb_pipes_per_fma,
        })

