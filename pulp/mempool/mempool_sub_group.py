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
# Discription: This file is the GVSoC configuration file for the MemPool Sub-group.
# Author: Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)
#         Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)

import gvsoc.runner
import cpu.iss.riscv as iss
from memory.memory import Memory
from pulp.snitch.hierarchical_cache import Hierarchical_cache
from vp.clock_domain import Clock_domain
import pulp.mempool.l1_subsystem as l1_subsystem
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from interco.interleaver import Interleaver
from pulp.idma.snitch_dma import SnitchDma
from interco.bus_watchpoint import Bus_watchpoint
from pulp.spatz.cluster_registers import Cluster_registers
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
import math
from pulp.mempool.mempool_tile import Tile
from pulp.mempool.hierarchical_interco import Hierarchical_Interco

GAPY_TARGET = True

class Sub_group(st.Component):

    def __init__(self, parent, name, parser, sub_group_id: int=0, group_id: int=0, nb_cores_per_tile: int=4, nb_sub_groups_per_group: int=4, nb_groups: int=4, total_cores: int=1024, bank_factor: int=4, axi_data_width: int=64):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters
        nb_remote_group_ports = nb_groups - 1
        nb_remote_sub_group_ports = nb_sub_groups_per_group - 1
        nb_tiles_per_sub_group = int((total_cores/nb_groups/nb_sub_groups_per_group)/nb_cores_per_tile)
        nb_banks_per_tile = nb_cores_per_tile * bank_factor

        ################################################################
        ##########              Design Components             ##########
        ################################################################
        # TIles
        self.tile_list = []
        for i in range(0, nb_tiles_per_sub_group):
            self.tile_list.append(Tile(self,f'tile_{i}',parser=parser,tile_id=i, sub_group_id=sub_group_id, group_id=group_id, nb_cores_per_tile=nb_cores_per_tile,
                nb_sub_groups_per_group=nb_sub_groups_per_group, nb_groups=nb_groups, total_cores=total_cores, bank_factor=bank_factor))

        #Sub Group local interconnect
        sub_group_local_interleaver = Interleaver(self, 'sub_group_local_interleaver', nb_slaves=nb_tiles_per_sub_group, nb_masters=nb_tiles_per_sub_group,
            interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor)), offset_translation=False)

        #Sub Group Remote Slave Interconnect
        sub_group_remote_master_interleavers = []
        for i in range(0, nb_remote_sub_group_ports):
            sub_group_remote_master_interleavers.append(Interleaver(self, f'sub_group_remote_slave_interleaver_{i}', nb_slaves=nb_tiles_per_sub_group, nb_masters=nb_tiles_per_sub_group, interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor)), offset_translation=False))

        sub_group_out_interfaces = []
        for port in range(0, nb_remote_group_ports):
            tile_itf_list = []
            for i in range(0, nb_tiles_per_sub_group):
                itf = router.Router(self, f'sub_group_remote_out_itf{port}_tile{i}', latency=2)
                itf.add_mapping('output')
                tile_itf_list.append(itf)
            sub_group_out_interfaces.append(tile_itf_list)

        # DMA network(virtual, to emulate multiple backends)
        # DMA TCDM Interface
        dma_tcdm_itf = router.Router(self, f'dma_tcdm_itf', bandwidth=axi_data_width)
        dma_tcdm_itf.add_mapping('output')

        # DMA TCDM Interleaver
        dma_tcdm_interleaver = DmaInterleaver(self, f'dma_tcdm_interleaver', nb_master_ports=1, nb_banks=nb_tiles_per_sub_group, bank_width=nb_banks_per_tile*4)

        # DMA AXI Interface
        dma_axi_itf = router.Router(self, f'dma_axi_itf', bandwidth=axi_data_width)
        dma_axi_itf.add_mapping('output')

        # SG-level AXI Interconnect
        # L2 cache rules
        l2_cache_rules = []
        l2_cache_rules.append((0x0000000C, 0x00000010))
        l2_cache_rules.append((0x00000008, 0x0000000C))
        l2_cache_rules.append((0xA0000000, 0xA0001000))
        l2_cache_rules.append((0x80000000, 0x80001000))

        # AXI Interconnect
        axi_ico = Hierarchical_Interco(self, 'axi_ico', enable_cache=True, cache_rules=l2_cache_rules, bandwidth=axi_data_width)

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################

        #Tile local master -> Group local interconnect
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(self.tile_list[i], 'loc_remt_master_out', sub_group_local_interleaver, 'in_%d' % i)

        #Group local interconnect -> Tile local slave
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(sub_group_local_interleaver, 'out_%d' % i, self.tile_list[i], 'loc_remt_slave_in')

        #Tile sub group remote master -> Sub Group remote interleavers
        for port in range(0, nb_remote_sub_group_ports):
            for i in range(0, nb_tiles_per_sub_group):
                self.bind(self.tile_list[i], f'sub_grp_remt{port}_master_out', sub_group_remote_master_interleavers[port], 'in_%d' % i)

        #Sub group remote interleavers -> Sub group remote routers
        for port in range(0, nb_remote_sub_group_ports):
            for i in range(0, nb_tiles_per_sub_group):
                self.bind(sub_group_remote_master_interleavers[port], 'out_%d' % i, sub_group_out_interfaces[port][i], 'input')

        #Tile axi port -> axi interconnect
        for i in range(0, nb_tiles_per_sub_group):
           self.bind(self.tile_list[i], 'axi_out', axi_ico, 'input')

        # DMA network(virtual, to emulate multiple backends)
        self.bind(dma_tcdm_itf, 'output', dma_tcdm_interleaver, 'input')
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(dma_tcdm_interleaver, f'out_{i}', self.tile_list[i], 'dma_tcdm')
        self.bind(dma_axi_itf, 'output', axi_ico, 'input')

        #Group loader -> Tile loader
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(self, 'loader_start', self.tile_list[i], 'loader_start')
            self.bind(self, 'loader_entry', self.tile_list[i], 'loader_entry')

        ################################################################
        ##########               Group Interfaces             ##########
        ################################################################

        # Remote TCDM interface between tiles to the group
        for port in range(0, nb_remote_group_ports):
            for i in range(0, nb_tiles_per_sub_group):
                self.bind(self, f'grp_remt{port}_tile{i}_slave_in', self.tile_list[i], f'grp_remt{port}_slave_in')
                self.bind(self.tile_list[i], f'grp_remt{port}_master_out', self, f'grp_remt{port}_tile{i}_master_out')

        # Remote TCDM interface between tiles to the sub group
        for port in range(0, nb_remote_sub_group_ports):
            for i in range(0, nb_tiles_per_sub_group):
                self.bind(self, f'sub_grp_remt{port+1}_tile{i}_slave_in', self.tile_list[i], f'sub_grp_remt{port}_slave_in')
                self.bind(sub_group_out_interfaces[port][i], 'output', self, f'sub_grp_remt{port+1}_tile{i}_master_out')

        # Propagate the barrier signals from the tiles to the group boundary
        for i in range(0, nb_tiles_per_sub_group):
            for j in range(0, nb_cores_per_tile):
                self.bind(self, f'barrier_ack_{i*nb_cores_per_tile+j}', self.tile_list[i], f'barrier_ack_{j}')

        # AXI
        self.bind(axi_ico, 'output', self, 'axi_out')

        # DMA
        self.bind(self, 'dma_tcdm', dma_tcdm_itf, 'input')
        self.bind(self, 'dma_axi', dma_axi_itf, 'input')
