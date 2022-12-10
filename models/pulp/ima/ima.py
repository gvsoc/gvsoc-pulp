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

class Ima(st.Component):

    def __init__(self, parent, name, nb_masters=32, xbar_x=256, xbar_y=256, statistics=False, silent=True):

        super(Ima, self).__init__(parent, name)

        self.set_component('pulp.ima.ima_v1_impl')

        self.add_properties({
            'nb_masters': nb_masters,
            'xbar_x': xbar_x,
            'xbar_y': xbar_y,
            'eval_ns': 130,
            'plot_write_ns': 1000,
            'plot_read_ns': 10,
            'statistics': statistics,
            'silent': silent,
        })

