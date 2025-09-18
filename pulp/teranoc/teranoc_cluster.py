#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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
# Discription: This file is the GVSoC configuration file for the TeraNoc Cluster.
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
#         Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)

import interco.router as router
import gvsoc.systree as st
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from interco.interleaver import Interleaver
import math
from pulp.teranoc.teranoc_group import Group
import pulp.teranoc.l1_noc as l1_noc

class Cluster(st.Component):

    def __init__(self, parent, name, parser, nb_cores_per_tile: int=4, nb_x_groups: int=4, nb_y_groups: int=4, total_cores: int=1024, nb_remote_ports_per_tile: int=2, bank_factor: int=4, axi_data_width: int=64, nb_axi_masters_per_group: int=1):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters
        nb_groups = nb_x_groups * nb_y_groups
        nb_tiles_per_group = int((total_cores/nb_groups)/nb_cores_per_tile)
        nb_banks_per_group = int(total_cores/nb_groups) * bank_factor
        nb_remote_ports_per_group = nb_tiles_per_group * nb_remote_ports_per_tile

        ################################################################
        ##########              Design Components             ##########
        ################################################################
        # TIles
        self.group_list = []
        for i in range(0, nb_x_groups):
            for j in range(0, nb_y_groups):
                group_id = i * nb_y_groups + j
                self.group_list.append(Group(self,f'group_{i}_{j}',parser=parser, group_id=group_id,
                    nb_cores_per_tile=nb_cores_per_tile, nb_x_groups=nb_x_groups, nb_y_groups=nb_y_groups, total_cores=total_cores,
                    nb_remote_ports_per_tile=nb_remote_ports_per_tile, bank_factor=bank_factor, axi_data_width=axi_data_width))

        self.l1_noc_list = []
        for i in range(0, nb_remote_ports_per_group):
            self.l1_noc_list.append(l1_noc.L1_noc(self, f'l1_noc_{i}', width=4, nb_x_groups=nb_x_groups, nb_y_groups=nb_y_groups,
                ni_outstanding_reqs=32, router_input_queue_size=2))

        # DMA TCDM Interface
        dma_tcdm_itf = router.Router(self, f'dma_tcdm_itf')
        dma_tcdm_itf.add_mapping('output')

        # DMA TCDM Interleaver
        dma_tcdm_interleaver = DmaInterleaver(self, f'dma_tcdm_interleaver', nb_master_ports=1, nb_banks=nb_groups, bank_width=nb_banks_per_group*4)

        # DMA AXI Interface
        dma_axi_itf = router.Router(self, f'dma_axi_itf')
        dma_axi_itf.add_mapping('output')

        # DMA AXI Interleaver
        dma_axi_interleaver = Interleaver(self, f'dma_axi_interleaver', nb_masters=1, nb_slaves=nb_groups, interleaving_bits=int(math.log2(nb_banks_per_group*4)), offset_translation=False)

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################

        #Group master output -> Group slave input
        for i in range(0, nb_x_groups):
            for j in range(0, nb_y_groups):
                for k in range(0, nb_remote_ports_per_group):
                    group_id = i * nb_y_groups + j
                    base = group_id * nb_tiles_per_group * nb_cores_per_tile * bank_factor * 1024
                    size = nb_tiles_per_group * nb_cores_per_tile * bank_factor * 1024
                    self.group_list[group_id].o_GROUP_OUTPUT(k, self.l1_noc_list[k].i_NARROW_INPUT(i, j))
                    self.l1_noc_list[k].o_NARROW_MAP(self.group_list[group_id].i_GROUP_INPUT(k), base=base, size=size, x=i, y=j)

        # Propagate barrier signals from group to cluster boundary
        for i in range(0, nb_groups):
            for j in range(0, nb_tiles_per_group):
                for k in range(0, nb_cores_per_tile):
                    self.bind(self, f'barrier_ack_{i*nb_cores_per_tile*nb_tiles_per_group+j*nb_cores_per_tile+k}', self.group_list[i], f'barrier_ack_{j*nb_cores_per_tile+k}')

        for i in range(0, nb_groups):
            self.bind(self, 'loader_start', self.group_list[i], 'loader_start')
            self.bind(self, 'loader_entry', self.group_list[i], 'loader_entry')

        for i in range(0, nb_groups):
            for j in range(0, nb_axi_masters_per_group):
                self.bind(self.group_list[i], f'axi_out_{j}', self, 'axi_%d' % (i * nb_axi_masters_per_group + j))

        for i in range(0, nb_groups):
            self.bind(self, 'rocache_cfg', self.group_list[i], 'rocache_cfg')

        self.bind(self, 'dma_tcdm', dma_tcdm_itf, 'input')
        self.bind(dma_tcdm_itf, 'output', dma_tcdm_interleaver, 'input')
        
        for i in range(0, nb_groups):
            self.bind(dma_tcdm_interleaver, f'out_{i}', self.group_list[i], 'dma_tcdm')

        self.bind(self, 'dma_axi', dma_axi_itf, 'input')
        self.bind(dma_axi_itf, 'output', dma_axi_interleaver, 'in_0')

        for i in range(0, nb_groups):
            self.bind(dma_axi_interleaver, f'out_{i}', self.group_list[i], 'dma_axi')
