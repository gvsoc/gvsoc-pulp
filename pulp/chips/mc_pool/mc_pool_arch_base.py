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

# Authors: Chi Zhang <chizhang@ethz.ch>, Marco Bertuletti <mbertuletti@ethz.ch>

import os


def get_arch_overrides(component, arch_cls):
    """
    Read gapy --config-opt overrides for every field arch_cls's default
    construction sets, via component.get_property(field_name)

    Usage (from McPoolSystem.__init__, which receives the systree Component
    as `self`):
        arch = McPoolArchTensorPool(**get_arch_overrides(self, McPoolArchTensorPool))
    Invoked as e.g.:
        ./install/bin/gvsoc --target=... --config-opt system/num_cluster_x=4 run
    """
    overrides = {}
    for field_name, default_value in vars(arch_cls()).items():
        value = component.get_property(field_name)
        if value is None:
            continue
        if isinstance(default_value, int) and isinstance(value, str):
            value = int(value, 0)
        overrides[field_name] = value
    return overrides


class McPoolArchBase:
    """
    Attributes shared identically by every multi-cluster MemPool
    configuration.

    Every attribute is also exported to the software as an ARCH_<NAME> define
    (see common/utils/config.py).
    """

    def __init__(self):

        #Cluster
        self.cluster_tcdm_base       = 0x00000000
        self.cluster_tcdm_remote     = 0x30000000

        self.cluster_stack_base      = 0x10000000
        self.cluster_stack_size      = 0x00020000

        self.cluster_reg_base        = 0x20000000
        self.cluster_reg_size        = 0x00000200

        self.instruction_mem_base    = 0x80000000

        # Mempool cluster configuration
        self.nb_groups               = 4

        # Tensor engines (RedMulE), disabled by default
        self.tensorpool              = False
        self.nb_redmule_tiles        = 0
        self.redmule_height          = 0
        self.redmule_width           = 0
        self.redmule_regs            = 0
        self.redmule_queue_depth     = 0
        self.redmule_stream_loads    = False
        # auto-derive: (height*(regs+1))//2
        self.redmule_tcdm_bank_number = None

        #HBM
        self.hbm_start_base          = 0xc0000000
        self.hbm_node_addr_space     = 0x00200000
        self.num_node_per_ctrl       = 1
        # Edge node aliasing is not supported by the NoC, this must stay 1
        self.hbm_node_aliase         = 1
        self.hbm_ctrl_xor_scrambling = 0
        self.hbm_ctrl_red_scrambling = 0
        # True models the HBM channels with DRAMSys, which needs SystemC and is much slower.
        # Otherwise they are plain memories of hbm_chan_size bytes, without the DRAM timing
        self.hbm_dramsys             = False
        self.hbm_chan_size           = 0x10000000

        #NoC
        self.noc_outstanding         = 64
        self.noc_link_width          = 512

        #System
        self.soc_register_base       = 0x90000000
        self.soc_register_size       = 0x00010000
        self.soc_register_eoc        = 0x90000000
        self.soc_register_wakeup     = 0x90000004

        #Synchronization
        self.sync_base               = 0x50000000
        self.sync_interleave         = 0x00000080
        self.sync_special_mem        = 0x00000040


class McPoolArchTensorPool(McPoolArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster_x           = 2
        self.num_cluster_y           = 5
        self.cluster_tcdm_size       = 0x00400000

        # Heterogeneous: num_cores PE (default 256, from the environment, as
        # the MemPool make variable) + 16 RedMulE tensor engines, sub-group
        # division (as Terapool) and a 4 MB L1 (as Terapool).
        self.terapool                = False
        self.num_core_per_cluster    = int(os.environ.get('NUM_CORES', 256))
        self.nb_cores_per_tile       = 4
        self.nb_sub_groups_per_group = 4
        self.bank_factor             = 8
        # Bank size keeping the L1 at cluster_tcdm_size
        self.bank_size               = self.cluster_tcdm_size // (
            self.num_core_per_cluster * self.bank_factor)
        self.axi_data_width          = 64
        self.dma_burst_queue_size    = 32
        self.nb_axi_masters_per_group = 4
        self.instruction_mem_size    = 0x400000
        self.nb_l2_banks             = 4

        self.tensorpool              = True
        self.nb_redmule_tiles        = int(os.environ.get('NUM_REDMULE_TILES', 16))
        self.redmule_height          = int(os.environ.get('REDMULE_HEIGHT', 8))
        self.redmule_width           = int(os.environ.get('REDMULE_WIDTH', 32))
        self.redmule_regs            = 3
        # Request queue entries (TE_QUEUE_DEPTH, as the single-cluster model)
        self.redmule_queue_depth     = int(os.environ.get('TE_QUEUE_DEPTH', 128))
        self.redmule_stream_loads    = True

        self.hbm_node_addr_space     = 0x00800000
        self.hbm_chan_placement      = [0, 2, 0, 2]
        for k, v in overrides.items():
            setattr(self, k, v)


class McPoolArchMemPool(McPoolArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster_x           = 4
        self.num_cluster_y           = 4
        self.cluster_tcdm_size       = 0x00100000

        self.terapool                = False
        self.num_core_per_cluster    = 256
        self.nb_cores_per_tile       = 4
        self.nb_sub_groups_per_group = 1
        self.bank_factor             = 4
        self.bank_size               = 1024
        self.axi_data_width          = 64
        self.dma_burst_queue_size    = 8
        self.nb_axi_masters_per_group = 1
        self.instruction_mem_size    = 0x400000
        self.nb_l2_banks             = 4

        self.hbm_chan_placement      = [4, 0, 0, 0]
        for k, v in overrides.items():
            setattr(self, k, v)


class McPoolArchTeraPool(McPoolArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster_x           = 4
        self.num_cluster_y           = 4
        self.cluster_tcdm_size       = 0x00400000

        self.terapool                = True
        self.num_core_per_cluster    = 1024
        self.nb_cores_per_tile       = 8
        self.nb_sub_groups_per_group = 4
        self.bank_factor             = 4
        self.bank_size               = 1024
        self.axi_data_width          = 64
        self.dma_burst_queue_size    = 8
        self.nb_axi_masters_per_group = 4
        self.instruction_mem_size    = 0x1000000
        self.nb_l2_banks             = 16

        self.hbm_chan_placement      = [4, 0, 0, 0]
        for k, v in overrides.items():
            setattr(self, k, v)


class McPoolArchMinPool(McPoolArchBase):
    def __init__(self, **overrides):
        super().__init__()
        self.num_cluster_x           = 2
        self.num_cluster_y           = 8
        self.cluster_tcdm_size       = 0x00010000

        self.terapool                = False
        self.num_core_per_cluster    = 16
        self.nb_cores_per_tile       = 4
        self.nb_sub_groups_per_group = 1
        self.bank_factor             = 4
        self.bank_size               = 1024
        self.axi_data_width          = 32
        self.dma_burst_queue_size    = 8
        self.nb_axi_masters_per_group = 1
        self.instruction_mem_size    = 0x400000
        self.nb_l2_banks             = 4

        # hbm_chan_placement[0] (west, y-direction) must be a power of two >=
        # num_cluster_y // num_node_per_ctrl = 8 // 1 = 8
        self.hbm_chan_placement      = [8, 0, 0, 0]
        for k, v in overrides.items():
            setattr(self, k, v)


CONFIGS = {
    "tensorpool": McPoolArchTensorPool,
    "mempool":    McPoolArchMemPool,
    "terapool":   McPoolArchTeraPool,
    "minpool":    McPoolArchMinPool,
}
