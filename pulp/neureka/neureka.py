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

class Neureka(st.Component):

    def __init__(self,
                parent,
                name,
                tp_in           : int=32,
                tp_in_s         : int=28,
                tp_out          : int=32,
                qa_in           : int=8,
                qa_out          : int=8,
                h_size          : int=6,
                w_size          : int=6,
                column_size     : int=9,
                block_size      : int=32,
                f_buffer_size   : int=8,
                filter_size     : int=3,
                shift_cycles    : int=2,
                overhead_ld_1x1 : int=19,
                overhead_ld_3x3 : int=31,
                overhead_mv     : int=17,
                quant_per_cycle : int=4):

        super(Neureka, self).__init__(parent, name)

        self.set_component('pulp.neureka.neureka')

        self.add_properties({
            "tp_in"             :   tp_in,
            "tp_in_s"           :   tp_in_s,
            "tp_out"            :   tp_out,
            "qa_in"             :   qa_in,
            "qa_out"            :   qa_out,
            "h_size"            :   h_size,
            "w_size"            :   w_size,
            "column_size"       :   column_size,
            "block_size"        :   block_size,
            "f_buffer_size"     :   f_buffer_size,
            "filter_size"       :   filter_size,
            "shift_cycles"      :   shift_cycles,
            "overhead_ld_1x1"   :   overhead_ld_1x1,
            "overhead_ld_3x3"   :   overhead_ld_3x3,
            "overhead_mv"       :   overhead_mv,
            "quant_per_cycle"   :   quant_per_cycle
        })
