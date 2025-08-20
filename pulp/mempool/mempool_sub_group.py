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
# Discription: This file is the GVSoC configuration file for the MemPool Tile.
# Author: Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)

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

        #Group Remote Slave Interconnect
        # group_remote_master_interfaces = []
        # for i in range(0, nb_remote_group_ports):
        #     group_remote_master_interfaces.append(Interleaver(self, f'group_remote_slave_interleaver_{i}', nb_slaves=nb_tiles_per_sub_group, nb_masters=nb_tiles_per_sub_group, interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor))))

        #Sub Group Remote Slave Interconnect
        sub_group_remote_master_interleavers = []
        for i in range(0, nb_remote_sub_group_ports):
            sub_group_remote_master_interleavers.append(Interleaver(self, f'sub_group_remote_slave_interleaver_{i}', nb_slaves=nb_tiles_per_sub_group, nb_masters=nb_tiles_per_sub_group, interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor)), offset_translation=False))

        sub_group_out_interfaces = []
        for port in range(0, nb_remote_group_ports):
            tile_itf_list = []
            for i in range(0, nb_tiles_per_sub_group):
                itf = router.Router(self, f'sub_group_remote_out_itf{port}_tile{i}', bandwidth=4, latency=2)
                itf.add_mapping('output')
                tile_itf_list.append(itf)
            sub_group_out_interfaces.append(tile_itf_list)

        #rom router
        rom_router = router.Router(self, 'rom_router', bandwidth=axi_data_width, latency=1)
        rom_router.add_mapping('output')

        #l2 router
        l2_router = router.Router(self, 'l2_router', bandwidth=axi_data_width, latency=1)
        l2_router.add_mapping('output')

        #csr router
        csr_router = router.Router(self, 'csr_router', bandwidth=32, latency=1)
        csr_router.add_mapping('output')

        #uart router
        uart_router = router.Router(self, 'uart_router', bandwidth=8, latency=1)
        uart_router.add_mapping('output')

        #dummy_mem router
        dummy_mem_router = router.Router(self, 'dummy_mem_router', bandwidth=32, latency=1)
        dummy_mem_router.add_mapping('output')

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################

        #Tile local master -> Group local interconnect
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(self.tile_list[i], 'loc_remt_master_out', sub_group_local_interleaver, 'in_%d' % i)

        #Group local interconnect -> Tile local slave
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(sub_group_local_interleaver, 'out_%d' % i, self.tile_list[i], 'loc_remt_slave_in')

        #Tile group remote master -> Group remote routers
        # for port in range(0, nb_remote_group_ports):
        #     for i in range(0, nb_tiles_per_sub_group):
        #         self.bind(self.tile_list[i], f'grp_remt{port}_master_out', group_remote_master_interfaces[port], 'in_%d' % i)

        #Tile sub group remote master -> Sub Group remote interleavers
        for port in range(0, nb_remote_sub_group_ports):
            for i in range(0, nb_tiles_per_sub_group):
                self.bind(self.tile_list[i], f'sub_grp_remt{port}_master_out', sub_group_remote_master_interleavers[port], 'in_%d' % i)

        #Sub group remote interleavers -> Sub group remote routers
        for port in range(0, nb_remote_sub_group_ports):
            for i in range(0, nb_tiles_per_sub_group):
                self.bind(sub_group_remote_master_interleavers[port], 'out_%d' % i, sub_group_out_interfaces[port][i], 'input')

        #Tile rom -> rom router
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(self.tile_list[i], 'rom', rom_router, 'input')

        #Tile l2 data -> l2 router
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(self.tile_list[i], 'L2_data', l2_router, 'input')

        #Tile l2 data -> csr router
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(self.tile_list[i], 'csr', csr_router, 'input')

        #Tile uart -> uart router
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(self.tile_list[i], 'uart', uart_router, 'input')

        #Tile l2 data -> dummy_mem router
        for i in range(0, nb_tiles_per_sub_group):
            self.bind(self.tile_list[i], 'dummy_mem', dummy_mem_router, 'input')

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

        # Other signals propagated to the group interface
        self.bind(rom_router, 'output', self, 'rom')
        self.bind(l2_router, 'output', self, 'L2_data')
        self.bind(csr_router, 'output', self, 'csr')
        self.bind(uart_router, 'output', self, 'uart')
        self.bind(dummy_mem_router, 'output', self, 'dummy_mem')
