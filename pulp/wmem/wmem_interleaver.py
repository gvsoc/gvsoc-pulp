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

class Wmem_interleaver(st.Component):

    def __init__(self, parent, name, nb_slaves: int, nb_masters: int, stage_bits: int=0):

        super(Wmem_interleaver, self).__init__(parent, name)

        self.set_component('pulp.wmem.wmem_impl')

        self.add_properties({
            'nb_slaves': nb_slaves,
            'nb_masters': nb_masters,
            'stage_bits': stage_bits
        })
