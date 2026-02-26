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

class SoftHierArch:

    def __init__(self):

        #Cluster
        self.num_cluster_x           = 4
        self.num_cluster_y           = 4
        self.num_core_per_cluster    = 3

        self.cluster_tcdm_bank_width = 32
        self.cluster_tcdm_bank_nb    = 128

        self.cluster_tcdm_base       = 0x00000000
        self.cluster_tcdm_size       = 0x00100000
        self.cluster_tcdm_remote     = 0x30000000

        self.cluster_stack_base      = 0x10000000
        self.cluster_stack_size      = 0x00020000

        self.cluster_zomem_base      = 0x18000000
        self.cluster_zomem_size      = 0x00020000

        self.cluster_reg_base        = 0x20000000
        self.cluster_reg_size        = 0x00000200

        #Spatz Vector Unit
        self.spatz_attaced_core_list = []
        self.spatz_num_vlsu_port     = 8
        self.spatz_num_function_unit = 8

        #IDMA
        self.idma_outstand_txn       = 16
        self.idma_outstand_burst     = 256

        #System
        self.instruction_mem_base    = 0x80000000
        self.instruction_mem_size    = 0x00010000
        self.main_memory_size        = 0x80000000

        self.soc_register_base       = 0x70000000
        self.soc_register_size       = 0x00010000
