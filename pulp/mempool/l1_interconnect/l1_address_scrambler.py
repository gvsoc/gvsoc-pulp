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


def _clog2(value: int) -> int:
    if value <= 1:
        return 0
    return (value - 1).bit_length()


class L1AddressScrambler(gvsoc.systree.Component):
    """L1 tile address scrambler (mempool / teranoc tile-side).

    Swaps the per-tile ``scramble`` field (LSB) with the ``tile_id`` field
    (MSB) inside the sequential-memory region ``[0, num_tiles *
    seq_mem_size_per_tile)``. Wraps the unified C++ component shared with the
    L2 wrapper (``pulp/mempool/common/address_scrambler/address_scrambler.cpp``).
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, *,
                 bypass: bool, num_tiles: int, seq_mem_size_per_tile: int,
                 byte_offset: int, num_banks_per_tile: int):
        super().__init__(parent, name)

        self.add_sources(['pulp/mempool/common/address_scrambler/address_scrambler.cpp'])

        seq_per_tile_bits = _clog2(seq_mem_size_per_tile)
        bank_offset_bits = _clog2(num_banks_per_tile)
        lsb_constant_bits = byte_offset + bank_offset_bits
        low_field_bits = max(seq_per_tile_bits - lsb_constant_bits, 0)
        high_field_bits = _clog2(num_tiles)

        self.add_properties({
            'bypass': bypass,
            'base_addr': 0,
            'size': num_tiles * seq_mem_size_per_tile,
            'lsb_constant_bits': lsb_constant_bits,
            'low_field_bits': low_field_bits,
            'high_field_bits': high_field_bits,
            'msb_constant_bits': 0,
        })
