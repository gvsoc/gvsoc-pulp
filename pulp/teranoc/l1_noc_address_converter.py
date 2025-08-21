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

class L1NocAddressConverter(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, bypass: bool = False, xbar_to_noc: bool = True, byte_offset: int=2, bank_size: int = 0, num_groups: int = 0, num_tiles_per_group: int = 0, num_banks_per_tile: int = 0):

        super().__init__(parent, name)

        self.add_sources(['pulp/teranoc/l1_noc_address_converter.cpp'])

        self.add_properties({
            "bypass": bypass,
            "xbar_to_noc": xbar_to_noc,
            "byte_offset": byte_offset,
            "bank_size": bank_size,
            "num_groups": num_groups,
            "num_tiles_per_group": num_tiles_per_group,
            "num_banks_per_tile": num_banks_per_tile
        })