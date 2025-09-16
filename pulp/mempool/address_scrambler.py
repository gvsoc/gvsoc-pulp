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

class AddressScrambler(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, bypass: bool = False, num_tiles: int = 1, seq_mem_size_per_tile: int = 0, byte_offset: int = 0, num_banks_per_tile: int = 0):

        super().__init__(parent, name)

        self.add_sources(['pulp/mempool/address_scrambler.cpp'])

        self.add_properties({
            "bypass": bypass,
            "num_tiles": num_tiles,
            "seq_mem_size_per_tile": seq_mem_size_per_tile,
            "byte_offset": byte_offset,
            "num_banks_per_tile": num_banks_per_tile
        })