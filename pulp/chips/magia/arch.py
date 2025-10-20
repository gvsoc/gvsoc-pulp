# Copyright (C) 2025 Fondazione Chips-IT

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

class MagiaArch:
    # Single tile address map from magia_tile_pkg.sv
    RESERVED_ADDR_START = 0x0000_0000
    RESERVED_SIZE       = 0x0000_FFFF
    RESERVED_ADDR_END   = RESERVED_ADDR_START + RESERVED_SIZE
    STACK_ADDR_START    = RESERVED_ADDR_END + 1
    STACK_SIZE          = 0x0000_FFFF
    STACK_ADDR_END      = STACK_ADDR_START + STACK_SIZE
    L1_ADDR_START       = STACK_ADDR_END + 1
    L1_SIZE             = 0x000D_FFFF
    L1_ADDR_END         = L1_ADDR_START + L1_SIZE
    L1_TILE_OFFSET      = 0x0010_0000
    L2_ADDR_START       = 0xC000_0000
    #L2_SIZE             = 0x4000_0000
    L2_SIZE             = 0xCC02_FFFF
    L2_ADDR_END         = L2_ADDR_START + L2_SIZE
    TEST_END_ADDR_START = L2_SIZE + 1
    TEST_END_SIZE       = 0x100
    STDOUT_ADDR_START   = 0xFFFF_0004
    STDOUT_SIZE         = 0x100

    # From magia_pkg.sv
    N_MEM_BANKS         = 32        # Number of TCDM banks
    N_WORDS_BANK        = 8192      # Number of words per TCDM bank

    # Extra
    BYTES_PER_WORD      = 4
    TILE_CLK_FREQ       = 200 * (10 ** 6)

    N_TILES_X           = 2
    N_TILES_Y           = 2
    NB_CLUSTERS         = N_TILES_X*N_TILES_Y