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
# Description: Architectural parameters for the TeraNoC family.
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
#
# Primitives live as dataclass fields (no defaults — every config must list
# every primitive, so each profile in CONFIGS is a self-contained snapshot).
# Derived structural values (nb_groups, nb_banks_per_tile, …) are computed
# from the primitives via @property — defined once, consistent everywhere,
# and every component reads them from a single source of truth.
#
# nb_snitch_per_tile counts only Snitch cores. Heterogeneous tiles may carry
# additional compute (RedMule, Spatz, …) which are not part of this number.

from dataclasses import dataclass


@dataclass(frozen=True)
class TeranocConfig:
    # Primitives (every profile must specify all of these).
    nb_snitch_per_tile:       int
    nb_tiles_per_group:       int
    nb_x_groups:              int
    nb_y_groups:              int
    bank_factor:              int
    l1_bank_bytes:            int
    l1_bank_width:            int   # bytes per L1 bank access (drives all L1-path bandwidths)
    nb_remote_ports_per_tile: int
    axi_data_width:           int
    nb_axi_masters_per_group: int
    l2_size:                  int
    nb_l2_banks:              int
    # L1 NoC router remapper: batch size for the rotating remap, and whether
    # ports within a batch are shuffled.
    l1_noc_remap_batch_size:  int
    l1_noc_remap_shuffle:     bool

    # ----- Derived -----
    @property
    def nb_groups(self):          return self.nb_x_groups * self.nb_y_groups
    @property
    def nb_tiles_total(self):     return self.nb_tiles_per_group * self.nb_groups
    @property
    def nb_banks_per_tile(self):  return self.nb_snitch_per_tile * self.bank_factor
    @property
    def nb_banks_per_group(self): return self.nb_banks_per_tile * self.nb_tiles_per_group
    @property
    def total_snitch(self):       return self.nb_snitch_per_tile * self.nb_tiles_total
    @property
    def l1_per_tile_bytes(self):  return self.nb_banks_per_tile * self.l1_bank_bytes
    @property
    def l1_total_bytes(self):     return self.l1_per_tile_bytes * self.nb_tiles_total
    @property
    def l2_bank_size(self):       return self.l2_size // self.nb_l2_banks
    @property
    def nb_remote_ports_per_group(self):
        return self.nb_tiles_per_group * self.nb_remote_ports_per_tile
    @property
    def nb_axi_masters(self):
        return self.nb_axi_masters_per_group * self.nb_groups
    @property
    def nb_local_ports(self):
        # Local-side L1 ports per tile. Today this is just the Snitch cores;
        # heterogeneous masters (HWPE sub-ports) will be added here later.
        return self.nb_snitch_per_tile
    @property
    def nb_remote_ports(self):
        # 1 intra-group neighbor port + N inter-group NoC ports.
        return 1 + self.nb_remote_ports_per_tile


TERANOC = TeranocConfig(
    nb_snitch_per_tile       = 4,
    nb_tiles_per_group       = 16,
    nb_x_groups              = 4,
    nb_y_groups              = 4,
    bank_factor              = 4,
    l1_bank_bytes            = 1024,
    l1_bank_width            = 4,
    nb_remote_ports_per_tile = 2,
    axi_data_width           = 64,
    nb_axi_masters_per_group = 1,
    l2_size                  = 0x1000000,
    nb_l2_banks              = 16,
    l1_noc_remap_batch_size  = 4,
    l1_noc_remap_shuffle     = True,
)

MEMPOOL_NOC = TeranocConfig(
    nb_snitch_per_tile       = 4,
    nb_tiles_per_group       = 16,
    nb_x_groups              = 2,
    nb_y_groups              = 2,
    bank_factor              = 4,
    l1_bank_bytes            = 1024,
    l1_bank_width            = 4,
    nb_remote_ports_per_tile = 2,
    axi_data_width           = 64,
    nb_axi_masters_per_group = 1,
    l2_size                  = 0x400000,
    nb_l2_banks              = 4,
    l1_noc_remap_batch_size  = 4,
    l1_noc_remap_shuffle     = True,
)

MINPOOL_NOC = TeranocConfig(
    nb_snitch_per_tile       = 4,
    nb_tiles_per_group       = 1,
    nb_x_groups              = 2,
    nb_y_groups              = 2,
    bank_factor              = 4,
    l1_bank_bytes            = 1024,
    l1_bank_width            = 4,
    nb_remote_ports_per_tile = 2,
    axi_data_width           = 32,
    nb_axi_masters_per_group = 1,
    l2_size                  = 0x400000,
    nb_l2_banks              = 4,
    l1_noc_remap_batch_size  = 4,
    l1_noc_remap_shuffle     = True,
)


CONFIGS = {
    'teranoc':     TERANOC,
    'mempool_noc': MEMPOOL_NOC,
    'minpool_noc': MINPOOL_NOC,
}

DEFAULT_CONFIG = 'teranoc'
