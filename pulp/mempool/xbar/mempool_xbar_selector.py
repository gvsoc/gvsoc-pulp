#
# Copyright (C) 2025 ETH Zurich and University of Bologna
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

import gvsoc.systree

class MempoolXbarSelector(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, interleaver_mode: bool=False, output_id: int=0, interleaving_bits: int=0, stage_bits: int=0):

        super().__init__(parent, name)

        self.add_sources(['pulp/mempool/xbar/mempool_xbar_selector.cpp'])

        self.add_properties({
            "interleaver_mode": interleaver_mode,
            "output_id": output_id,
            "interleaving_bits": interleaving_bits,
            "stage_bits": stage_bits
        })