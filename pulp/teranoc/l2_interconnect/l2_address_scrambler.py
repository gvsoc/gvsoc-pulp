#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
#

import gvsoc.systree


def _clog2(value: int) -> int:
    if value <= 1:
        return 0
    return (value - 1).bit_length()


class L2AddressScrambler(gvsoc.systree.Component):
    """L2 bank-grid address scrambler (teranoc).

    Swaps the bank-id ``scramble`` field with the ``remainder`` field inside
    ``[l2_base_addr, l2_base_addr + l2_size)``. Wraps the unified C++ component
    shared with the L1 wrapper
    (``pulp/mempool/common/address_scrambler/address_scrambler.cpp``).
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, *,
                 bypass: bool, l2_base_addr: int, l2_size: int,
                 nb_banks: int, bank_width: int, interleave: int):
        super().__init__(parent, name)

        self.add_sources(['pulp/mempool/common/address_scrambler/address_scrambler.cpp'])

        addr_width = 32
        lsb_constant_bits = _clog2(bank_width * interleave)
        msb_constant_bits = addr_width - _clog2(l2_size)
        # Preserve the legacy nb_banks==1 quirk: scramble width is forced to 1
        # so the single-bank case still walks through the swap path.
        low_field_bits = 1 if nb_banks == 1 else _clog2(nb_banks)
        high_field_bits = (addr_width - low_field_bits
                           - lsb_constant_bits - msb_constant_bits)

        self.add_properties({
            'bypass': bypass,
            'base_addr': l2_base_addr,
            'size': l2_size,
            'lsb_constant_bits': lsb_constant_bits,
            'low_field_bits': low_field_bits,
            'high_field_bits': high_field_bits,
            'msb_constant_bits': msb_constant_bits,
        })
