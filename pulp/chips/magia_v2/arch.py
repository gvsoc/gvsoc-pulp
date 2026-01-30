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

from gvrun.attribute import Tree, Value

class MagiaArch:
    # Single tile address map from magia_tile_pkg.sv
    REDMULE_CTRL_ADDR_START = 0x0000_0100
    REDMULE_CTRL_SIZE       = 0x0000_00FF
    REDMULE_CTRL_ADDR_END   = REDMULE_CTRL_ADDR_START + REDMULE_CTRL_SIZE
    IDMA_CTRL_ADDR_START    = REDMULE_CTRL_ADDR_END + 1
    IDMA_CTRL_SIZE          = 0x0000_03FF
    IDMA_CTRL_ADDR_END      = IDMA_CTRL_ADDR_START + IDMA_CTRL_SIZE
    FSYNC_CTRL_ADDR_START   = IDMA_CTRL_ADDR_END + 1
    FSYNC_CTRL_SIZE         = 0x0000_00FF
    FSYNC_CTRL_ADDR_END     = FSYNC_CTRL_ADDR_START + FSYNC_CTRL_SIZE
    EVENT_UNIT_ADDR_START   = FSYNC_CTRL_ADDR_END + 1
    EVENT_UNIT_SIZE         = 0x0000_0FFF
    EVENT_UNIT_ADDR_END     = EVENT_UNIT_ADDR_START + EVENT_UNIT_SIZE
    SPATZ_CTRL_START        = EVENT_UNIT_ADDR_END + 1
    SPATZ_CTRL_SIZE         = 0x0000_00FF
    SPATZ_CTRL_END          = SPATZ_CTRL_START + SPATZ_CTRL_SIZE
    RESERVED_ADDR_START     = SPATZ_CTRL_END + 1
    RESERVED_SIZE           = 0x0000_E7FF
    RESERVED_ADDR_END       = RESERVED_ADDR_START + RESERVED_SIZE
    STACK_ADDR_START        = RESERVED_ADDR_END + 1
    STACK_SIZE              = 0x0000_FFFF
    STACK_ADDR_END          = STACK_ADDR_START + STACK_SIZE
    L1_ADDR_START           = STACK_ADDR_END + 1
    L1_SIZE                 = 0x000D_FFFF
    L1_ADDR_END             = L1_ADDR_START + L1_SIZE
    L1_TILE_OFFSET          = 0x0010_0000
    L2_ADDR_START           = 0xC000_0000
    L2_SIZE                 = 0x0CFE_FFFF # here in RTL we have 0x4000_0000 but the end address (TEST_END_ADDR_START) then will fall in L2... no sense to me
    #L2_SIZE                 = 0x0C02_FFFF # temp L2 size to match Luca Balboni bare-metal MMAP
    L2_ADDR_END             = L2_ADDR_START + L2_SIZE
    TEST_END_ADDR_START     = L2_ADDR_END + 1
    TEST_END_SIZE           = 0x400
    STDOUT_ADDR_START       = 0xFFFF_0004
    STDOUT_SIZE             = 0x100

    # Snitch_Spatz
    SPATZ_BOOTROM_ADDR         = 0x1000_0000
    SPATZ_BOOTROM_SIZE         = 0x100
    SPATZ_ROMFILE              = 'pulp/snitch/bootrom_spatz.bin'

    # From magia_pkg.sv
    N_MEM_BANKS         = 32        # Number of TCDM banks
    N_WORDS_BANK        = 8192      # Number of words per TCDM bank

    # Extra
    BYTES_PER_WORD      = 4
    TILE_CLK_FREQ       = 200 * (10 ** 6)

    N_TILES_X           = 4
    N_TILES_Y           = 4

    USE_NARROW_WIDE     = False
    ENABLE_SPATZ        = True

class MagiaTree(Tree):
    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.N_TILES_X = Value(self, 'n_tiles_x', MagiaArch.N_TILES_X, cast=int,
            description='Number of tiles on X dimension')
        self.N_TILES_Y = Value(self, 'n_tiles_y', MagiaArch.N_TILES_Y, cast=int,
            description='Number of tiles on Y dimension')

        self.NB_CLUSTERS = self.N_TILES_X*self.N_TILES_Y

        self.romfile = Value(self, 'spatz_romfile', MagiaArch.SPATZ_ROMFILE, cast=str,
            description='Snitch_Spatz rom file')

class MagiaDSE:
    SOC_L2_LATENCY              = 2
    TILE_ICACHE_REFILL_LATENCY  = 2
    TILE_TCDM_LATENCY           = 1
    TILE_AXI_XBAR_LATENCY       = 2
    TILE_AXI_XBAR_SYNC          = False
    TILE_OBI_XBAR_LATENCY       = 2
    TILE_OBI_XBAR_SYNC          = True
    TILE_IDMA0_BQUEUE_SIZE      = 2
    TILE_IDMA0_B_SIZE           = 4
    TILE_IDMA1_BQUEUE_SIZE      = 2
    TILE_IDMA1_B_SIZE           = 8