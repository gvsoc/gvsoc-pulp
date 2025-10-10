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

class L2_AddressScrambler(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, bypass: bool, l2_base_addr: int, l2_size: int, nb_banks: int, bank_width: int, interleave: int):

        super().__init__(parent, name)

        self.add_sources(['pulp/teranoc/l2_interconnect/l2_address_scrambler.cpp'])

        self.add_properties({
            "bypass": bypass,
            "l2_base_addr": l2_base_addr,
            "l2_size": l2_size,
            "nb_banks": nb_banks,
            "bank_width": bank_width,
            "interleave": interleave
        })