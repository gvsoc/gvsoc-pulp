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
from gvrun.parameter import TargetParameter

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
    CLUSTER_CTRL_START      = EVENT_UNIT_ADDR_END + 1
    CLUSTER_CTRL_SIZE       = 0x0000_00FF
    CLUSTER_CTRL_END        = CLUSTER_CTRL_START + CLUSTER_CTRL_SIZE
    RESERVED_ADDR_START     = CLUSTER_CTRL_END + 1
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
    L2_ADDR_END             = L2_ADDR_START + L2_SIZE
    TEST_END_ADDR_START     = L2_ADDR_END + 1
    TEST_END_SIZE           = 0x400
    STDOUT_ADDR_START       = 0xFFFF_0004
    STDOUT_SIZE             = 0x100

    # From magia_pkg.sv
    N_MEM_BANKS         = 32        # Number of TCDM banks
    N_WORDS_BANK        = 8192      # Number of words per TCDM bank

    # Extra
    BYTES_PER_WORD      = 4
    TILE_CLK_FREQ       = 200 * (10 ** 6)

    # Snitch_Spatz
    SPATZ_ENABLE               = True
    SPATZ_BOOTROM_ADDR         = 0x1000_0000
    SPATZ_BOOTROM_SIZE         = 0x100
    SPATZ_ROMFILE              = ''
    USE_NEW_SPATZ              = True

    # Pulp Cores
    PULP_ENABLE         = True
    NB_PULP_CORES       = 8

    # Tiles assignment
    N_TILES_X           = 1
    N_TILES_Y           = 1

    ENABLE_PCIE_VFIO            = False

class MagiaTree(Tree):
    """Customizable knobs of the platform.

    The tree-reshaping knobs (mesh size n_tiles_x/n_tiles_y and the per-tile
    nb_pulp_cores) are **target parameters**, i.e. they belong to the target
    *name*, not to the run command line:

        make TARGETS="magia_v3:n_tiles_x=2,n_tiles_y=2" build
        gvrun --target=magia_v3:n_tiles_x=2,n_tiles_y=2 --param binary=<elf> run

    gvsoc bakes the whole component tree (its shape + every model's typed
    config) into libplatform_tree_<target>.so and checks it against the systree
    it rebuilds at run time. Reshaping the tree from a run-time --attr
    invalidates that library and drops to the JSON fallback, which cannot carry
    the typed (io_v2 / iss_v2) configurations -> a component comes up with an
    uninitialized config and the sim dies (std::bad_alloc) for no visible
    reason. Embedding the reshaping knobs in the target name makes build and run
    coherent by construction. Hence these are TargetParameters, not Values:
    --attr magia_v3/n_tiles_x=... is now rejected instead of silently
    mismatching.

    spatz_romfile stays a plain run-time attribute (--attr
    magia_v3/spatz_romfile=<path>) on purpose: it is a stim_file path that
    differs between build and run and is overlaid at run time, never entering
    the compiled tree.
    """
    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.n_tiles_x = TargetParameter(parent, name='n_tiles_x',
            value=MagiaArch.N_TILES_X, cast=int,
            description='Number of tiles on X dimension').value
        self.n_tiles_y = TargetParameter(parent, name='n_tiles_y',
            value=MagiaArch.N_TILES_Y, cast=int,
            description='Number of tiles on Y dimension').value

        self.nb_clusters = self.n_tiles_x*self.n_tiles_y

        if MagiaArch.SPATZ_ENABLE:
            self.romfile = Value(self, 'spatz_romfile', MagiaArch.SPATZ_ROMFILE, cast=str,
                description='Snitch_Spatz rom file')
            print("SNITCH_SPATZ complex enabled")

        if MagiaArch.PULP_ENABLE:
            self.nb_pulp_cores = TargetParameter(parent, name='nb_pulp_cores',
                value=MagiaArch.NB_PULP_CORES, cast=int,
                description='Number of pulp cores').value
            print(f"PULP complex enabled with {self.nb_pulp_cores} cores")

class MagiaDSE:
    SOC_L2_LATENCY              = 2
    TILE_ICACHE_REFILL_LATENCY  = 2
    TILE_TCDM_LATENCY           = 1
    TILE_AXI_XBAR_LATENCY       = 2
    TILE_AXI_XBAR_SYNC          = False
    TILE_OBI_XBAR_LATENCY       = 2
    TILE_OBI_XBAR_SYNC          = True
    TILE_IDMA0_BQUEUE_SIZE      = 2
    TILE_IDMA0_B_SIZE           = 32
    TILE_IDMA1_BQUEUE_SIZE      = 2
    TILE_IDMA1_B_SIZE           = 32