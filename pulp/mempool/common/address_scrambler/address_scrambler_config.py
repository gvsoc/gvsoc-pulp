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

from config_tree import Config, cfg_field


class AddressScramblerConfig(Config):
    """Configuration of the address scrambler shared by the L1 (mempool / teranoc tile) and
    L2 (teranoc bank grid) wrappers (address_scrambler.cpp). Inside
    [base_addr, base_addr + size), the address is cut, from the LSB, in lsb_constant_bits,
    low_field_bits, high_field_bits and msb_constant_bits, and the low and high fields are
    swapped."""

    bypass: bool = cfg_field(default=False, desc="True if addresses go through unchanged")
    base_addr: int = cfg_field(default=0, fmt="hex", desc="Base address of the scrambled region")
    size: int = cfg_field(default=0, fmt="hex", desc="Size of the scrambled region")
    lsb_constant_bits: int = cfg_field(default=0, desc="Number of LSB bits kept in place")
    low_field_bits: int = cfg_field(default=0, desc="Width of the low field, swapped with the high one")
    high_field_bits: int = cfg_field(default=0, desc="Width of the high field, swapped with the low one")
    msb_constant_bits: int = cfg_field(default=0, desc="Number of MSB bits kept in place")
