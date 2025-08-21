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

import gvsoc.runner
import cpu.iss.riscv as iss
from memory.memory import Memory
from pulp.snitch.hierarchical_cache import Hierarchical_cache
from vp.clock_domain import Clock_domain
import pulp.teranoc.l1_noc as l1_noc
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from interco.interleaver import Interleaver
from pulp.idma.snitch_dma import SnitchDma
from interco.bus_watchpoint import Bus_watchpoint
from pulp.spatz.cluster_registers import Cluster_registers
from elftools.elf.elffile import *
import math
from pulp.teranoc.teranoc_group import Group

GAPY_TARGET = True

class Cluster(st.Component):

    def __init__(self, parent, name, parser, terapool: bool=False, nb_cores_per_tile: int=4, nb_x_groups: int=4, nb_y_groups: int=4, total_cores: int=1024, nb_remote_ports_per_tile: int=2, bank_factor: int=4, axi_data_width: int=64):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters
        nb_groups = nb_x_groups * nb_y_groups
        nb_tiles_per_group = int((total_cores/nb_groups)/nb_cores_per_tile)
        nb_remote_ports_per_group = nb_tiles_per_group * nb_remote_ports_per_tile

        ################################################################
        ##########              Design Components             ##########
        ################################################################
        # TIles
        self.group_list = []
        for i in range(0, nb_x_groups):
            for j in range(0, nb_y_groups):
                group_id = i * nb_y_groups + j
                self.group_list.append(Group(self,f'group_{i}_{j}',parser=parser, terapool=terapool, group_id=group_id,
                    nb_cores_per_tile=nb_cores_per_tile, nb_x_groups=nb_x_groups, nb_y_groups=nb_y_groups, total_cores=total_cores,
                    nb_remote_ports_per_tile=nb_remote_ports_per_tile, bank_factor=bank_factor, axi_data_width=axi_data_width))

        self.l1_noc_list = []
        for i in range(0, nb_remote_ports_per_group):
            self.l1_noc_list.append(l1_noc.L1_noc(self, f'l1_noc_{i}', width=4, nb_x_groups=nb_x_groups, nb_y_groups=nb_y_groups,
                ni_outstanding_reqs=32, router_input_queue_size=2))

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
            self.bind(self.group_list[i], 'rom', self, 'rom_%d' % i)
            self.bind(self.group_list[i], 'L2_data', self, 'L2_data_%d' % i)
            self.bind(self.group_list[i], 'csr', self, 'csr_%d' % i)
            self.bind(self.group_list[i], 'uart', self, 'uart_%d' % i)
            self.bind(self.group_list[i], 'dummy_mem', self, 'dummy_mem_%d' % i)
            self.bind(self, 'loader_start', self.group_list[i], 'loader_start')
            self.bind(self, 'loader_entry', self.group_list[i], 'loader_entry')
