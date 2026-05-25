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
import math
from pulp.teranoc.teranoc_group import TeranocGroup
from pulp.teranoc.l2_interconnect.l2_address_scrambler import L2AddressScrambler
from pulp.teranoc.l1_interconnect.l1_noc import L1_noc

class TeranocCluster(st.Component):

    def __init__(self, parent, name, parser, arch):
        super().__init__(parent, name)

        ################################################################
        ##########              Design Components             ##########
        ################################################################
        # Groups
        self.group_list = []
        for i in range(0, arch.nb_x_groups):
            for j in range(0, arch.nb_y_groups):
                self.group_list.append(TeranocGroup(self, f'group_{i}_{j}', parser=parser, arch=arch,
                    group_id_x=i, group_id_y=j))

        self.l1_req_noc_list = []
        for i in range(0, arch.nb_remote_ports_per_group):
            self.l1_req_noc_list.append(L1_noc(self, f'l1_req_noc_{i}', width=arch.l1_bank_width, nb_x_groups=arch.nb_x_groups, nb_y_groups=arch.nb_y_groups, router_input_queue_size=2, router_output_queue_size=2))

        self.l1_resp_noc_list = []
        for i in range(0, arch.nb_remote_ports_per_group):
            self.l1_resp_noc_list.append(L1_noc(self, f'l1_resp_noc_{i}', width=arch.l1_bank_width, nb_x_groups=arch.nb_x_groups, nb_y_groups=arch.nb_y_groups, router_input_queue_size=2, router_output_queue_size=2))

        l2_addr_scrambler_list = []
        for i in range(0, arch.nb_x_groups):
            for j in range(0, arch.nb_y_groups):
                for k in range(0, arch.nb_axi_masters_per_group):
                    l2_addr_scrambler_list.append(L2AddressScrambler(self, f'l2_addr_scrambler_{i}_{j}_{k}', bypass=False, l2_base_addr=0x80000000, l2_size=arch.l2_size, nb_banks=arch.nb_l2_banks, bank_width=arch.axi_data_width, interleave=16))

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################
        # L1 req noc <--> Group l1 noc req interface
        for i in range(0, arch.nb_x_groups):
            for j in range(0, arch.nb_y_groups):
                group_id = i * arch.nb_y_groups + j
                for k in range(0, arch.nb_remote_ports_per_group):
                    self.bind(self.group_list[group_id], f'l1_noc_req_mst_{k}', self.l1_req_noc_list[k], f'in_{i}_{j}')
                    self.bind(self.l1_req_noc_list[k], f'out_{i}_{j}', self.group_list[group_id], f'l1_noc_req_slv_{k}')

        # L1 resp noc <--> Group l1 noc resp interface
        for i in range(0, arch.nb_x_groups):
            for j in range(0, arch.nb_y_groups):
                group_id = i * arch.nb_y_groups + j
                for k in range(0, arch.nb_remote_ports_per_group):
                    self.bind(self.l1_resp_noc_list[k], f'out_{i}_{j}', self.group_list[group_id], f'l1_noc_resp_mst_{k}')
                    self.bind(self.group_list[group_id], f'l1_noc_resp_slv_{k}', self.l1_resp_noc_list[k], f'in_{i}_{j}')

        # Group axi port -> L2 addr scrambler
        for i in range(0, arch.nb_x_groups):
            for j in range(0, arch.nb_y_groups):
                group_id = i * arch.nb_y_groups + j
                for k in range(0, arch.nb_axi_masters_per_group):
                    self.bind(self.group_list[group_id], f'axi_out_{k}', l2_addr_scrambler_list[group_id*arch.nb_axi_masters_per_group+k], 'input')

        # Propagate barrier signals from group to cluster boundary
        for i in range(0, arch.nb_groups):
            for j in range(0, arch.nb_tiles_per_group):
                for k in range(0, arch.nb_snitch_per_tile):
                    self.bind(self, f'barrier_ack_{i*arch.nb_snitch_per_tile*arch.nb_tiles_per_group+j*arch.nb_snitch_per_tile+k}', self.group_list[i], f'barrier_ack_{j*arch.nb_snitch_per_tile+k}')

        for i in range(0, arch.nb_groups):
            self.bind(self, 'loader_start', self.group_list[i], 'loader_start')
            self.bind(self, 'loader_entry', self.group_list[i], 'loader_entry')

        for i in range(0, arch.nb_x_groups):
            for j in range(0, arch.nb_y_groups):
                group_id = i * arch.nb_y_groups + j
                for k in range(0, arch.nb_axi_masters_per_group):
                    self.bind(l2_addr_scrambler_list[group_id * arch.nb_axi_masters_per_group + k], 'output', self, f'axi_{i}_{j}_{k}')

        for i in range(0, arch.nb_groups):
            self.bind(self, 'rocache_cfg', self.group_list[i], 'rocache_cfg')

        for i in range(0, arch.nb_x_groups):
            for j in range(0, arch.nb_y_groups):
                group_id = i * arch.nb_y_groups + j
                self.bind(self, f'dma_tcdm_{group_id}', self.group_list[group_id], 'dma_tcdm')
                self.bind(self, f'dma_axi_{group_id}', self.group_list[group_id], 'dma_axi')
