#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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

# Author: Chi Zhang <chizhang@ethz.ch>

class FlexClusterArch:

    def __init__(self):

        #Cluster
        # 256 cores/cluster (confirmed fast/safe to build in gapy, unlike
        # 512), mesh enlarged to 2x5 = 10 clusters to fit
        # l1_transformer_mc_f16's N_BEAM=8 + N_ATTN=2 active clusters (the
        # 4x2=8-cluster known-good baseline wasn't enough on its own). x=2
        # keeps the north/south HBM channel count a small power of two;
        # y=5 is unconstrained (west/east HBM disabled).
        self.num_cluster_x           = 2
        self.num_cluster_y           = 5

        self.cluster_tcdm_base       = 0x00000000
        self.cluster_tcdm_size       = 0x00400000
        self.cluster_tcdm_remote     = 0x30000000

        self.cluster_stack_base      = 0x10000000
        self.cluster_stack_size      = 0x00020000

        self.cluster_reg_base        = 0x20000000
        self.cluster_reg_size        = 0x00000200

        self.instruction_mem_base    = 0x80000000

        # Mempool cluster configuration (Tensorpool)
        # Heterogeneous: 512 PE (as Mempool) + 16 RedMulE tensor engines,
        # sub-group division (as Terapool) and a 4 MB L1 (as Terapool).
        self.terapool                    = False
        self.num_core_per_cluster        = 256
        self.nb_cores_per_tile           = 4
        self.nb_sub_groups_per_group     = 4
        self.nb_groups                   = 4
        self.bank_factor                 = 8
        self.bank_size                   = 2048
        self.axi_data_width              = 64
        self.dma_tcdm_outstanding        = 32
        self.dma_burst_queue_size        = 32
        self.nb_axi_masters_per_group    = 4
        self.instruction_mem_size        = 0x400000
        self.nb_l2_banks                 = 4

        # Tensor engines (RedMulE)
        self.tensorpool                  = True
        self.nb_redmule_tiles            = 16
        self.redmule_height              = 4
        self.redmule_width               = 8
        self.redmule_regs                = 3
        self.redmule_queue_depth         = 128
        self.redmule_stream_loads        = True
        self.redmule_tcdm_bank_number    = None   # auto-derive: (height*(regs+1))//2

        #HBM
        self.hbm_start_base          = 0xc0000000
        self.hbm_node_addr_space     = 0x00800000
        self.num_node_per_ctrl       = 1
        # hbm_chan_placement[1]/[3] (north/south, x-direction) must each be a
        # power of two >= num_cluster_x // num_node_per_ctrl = 2 // 1 = 2;
        # west/east (index 0/2) disabled -- 2 north + 2 south = 4 channels.
        self.hbm_chan_placement      = [0,2,0,2]
        self.hbm_node_aliase         = 1

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
