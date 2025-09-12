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
# Discription: This file is the GVSoC configuration file for the MemPool Group.
# Author: Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)
#         Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)

import gvsoc.systree as st
import interco.router as router
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from interco.interleaver import Interleaver
import math
from pulp.mempool.mempool_tile import Tile
from pulp.mempool.mempool_sub_group import Sub_group
from pulp.mempool.hierarchical_interco import Hierarchical_Interco

class Group(st.Component):

    def __init__(self, parent, name, parser, terapool: bool=False, group_id: int=0, nb_cores_per_tile: int=4, nb_sub_groups_per_group: int=1, nb_groups: int=4, total_cores: int= 256, bank_factor: int=4, axi_data_width: int=64):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters
        if terapool:
            nb_remote_group_ports = nb_groups - 1
            nb_tiles_per_sub_group = int((total_cores/nb_groups/nb_sub_groups_per_group)/nb_cores_per_tile)
            nb_banks_per_sub_group = int((total_cores/nb_groups/nb_sub_groups_per_group)) * bank_factor
        else:
            nb_remote_ports = nb_groups - 1
            nb_tiles_per_group = int((total_cores/nb_groups)/nb_cores_per_tile)
            nb_banks_per_tile = nb_cores_per_tile * bank_factor

        ################################################################
        ##########              Design Components             ##########
        ################################################################
        # Next-level components
        if terapool:
            # Sub groups
            self.sub_group_list = []
            for i in range(0, nb_sub_groups_per_group):
                self.sub_group_list.append(Sub_group(self,f'sub_group_{i}',parser=parser, sub_group_id=i, group_id=group_id, nb_cores_per_tile=nb_cores_per_tile,
                    nb_sub_groups_per_group=nb_sub_groups_per_group, nb_groups=nb_groups, total_cores=total_cores, bank_factor=bank_factor, axi_data_width=axi_data_width))
        else:
            # TIles
            self.tile_list = []
            for i in range(0, nb_tiles_per_group):
                self.tile_list.append(Tile(self,f'tile_{i}',parser=parser, tile_id=i, sub_group_id=0, group_id=group_id, nb_cores_per_tile=nb_cores_per_tile,
                    nb_sub_groups_per_group=1, nb_groups=nb_groups, total_cores=total_cores, bank_factor=bank_factor, axi_data_width=axi_data_width))

        # TCDM Interconnect
        if terapool:
            #Group Remote Slave Interconnect
            group_remote_master_interleavers = []
            for i in range(0, nb_remote_group_ports):
                group_remote_master_interleavers.append(Interleaver(self, f'group_remote_slave_interleaver_{i}', nb_slaves=nb_sub_groups_per_group*nb_tiles_per_sub_group,
                    nb_masters=nb_sub_groups_per_group*nb_tiles_per_sub_group, interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor)), offset_translation=False))

            group_remote_out_interfaces = []
            for port in range(0, nb_remote_group_ports):
                sub_group_itf_list = []
                for i in range(0, nb_sub_groups_per_group):
                    tile_itf_list = []
                    for j in range(0, nb_tiles_per_sub_group):
                        itf = router.Router(self, f'group_remote_out_itf{port}_sg{i}_tile{j}', latency=4)
                        itf.add_mapping('output')
                        tile_itf_list.append(itf)
                    sub_group_itf_list.append(tile_itf_list)
                group_remote_out_interfaces.append(sub_group_itf_list)
        else:
            #Group local interconnect
            group_local_interleaver = Interleaver(self, 'group_local_interleaver', nb_slaves=nb_tiles_per_group, nb_masters=nb_tiles_per_group,
                interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor)), offset_translation=False)

            #Group Remote Slave Interconnect
            group_remote_master_interleavers = []
            for i in range(0, nb_remote_ports):
                group_remote_master_interleavers.append(Interleaver(self, f'group_remote_slave_interleaver_{i}', nb_slaves=nb_tiles_per_group, nb_masters=nb_tiles_per_group, interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor)), offset_translation=False))

            group_remote_out_interfaces = []
            for port in range(0, nb_remote_ports):
                tile_itf_list = []
                for i in range(0, nb_tiles_per_group):
                    itf = router.Router(self, f'group_remote_out_itf{port}_tile{i}', latency=2 if nb_tiles_per_group>1 else 0)
                    itf.add_mapping('output')
                    tile_itf_list.append(itf)
                group_remote_out_interfaces.append(tile_itf_list)

        # DMA network(virtual, to emulate multiple backends)
        if terapool:
            # DMA TCDM Interface
            dma_tcdm_itf = router.Router(self, f'dma_tcdm_itf')
            dma_tcdm_itf.add_mapping('output')

            # DMA TCDM Interleaver
            dma_tcdm_interleaver = DmaInterleaver(self, f'dma_tcdm_interleaver', nb_master_ports=1, nb_banks=nb_sub_groups_per_group, bank_width=nb_banks_per_sub_group*4)

            # DMA AXI Interface
            dma_axi_itf = router.Router(self, f'dma_axi_itf')
            dma_axi_itf.add_mapping('output')

            # DMA AXI Interleaver
            dma_axi_interleaver = Interleaver(self, f'dma_axi_interleaver', nb_masters=1, nb_slaves=nb_sub_groups_per_group, interleaving_bits=int(math.log2(nb_banks_per_sub_group*4)), offset_translation=False)
        else:
            # DMA TCDM Interface
            dma_tcdm_itf = router.Router(self, f'dma_tcdm_itf', bandwidth=axi_data_width)
            dma_tcdm_itf.add_mapping('output')

            # DMA TCDM Interleaver
            dma_tcdm_interleaver = DmaInterleaver(self, f'dma_tcdm_interleaver', nb_master_ports=1, nb_banks=nb_tiles_per_group, bank_width=nb_banks_per_tile*4)

            # DMA AXI Interface
            dma_axi_itf = router.Router(self, f'dma_axi_itf', bandwidth=axi_data_width)
            dma_axi_itf.add_mapping('output')

        # Group-level AXI Interconnect, does not exist in Terapool
        if not terapool:
            # L2 cache rules
            l2_cache_rules = []
            l2_cache_rules.append((0x0000000C, 0x00000010))
            l2_cache_rules.append((0x00000008, 0x0000000C))
            l2_cache_rules.append((0xA0000000, 0xA0001000))
            l2_cache_rules.append((0x80000000, 0x80001000))

            # AXI Interconnect
            axi_ico = Hierarchical_Interco(self, 'axi_ico', enable_cache=True, cache_rules=l2_cache_rules, bandwidth=axi_data_width)

        # AXI Interface
        if terapool:
            axi_itf = []
            for i in range(0, nb_sub_groups_per_group):
                itf = router.Router(self, f'axi_itf_{i}', bandwidth=axi_data_width, latency=2)
                itf.add_mapping('output')
                axi_itf.append(itf)
        else:
            axi_itf = router.Router(self, 'axi_itf', bandwidth=axi_data_width, latency=2)
            axi_itf.add_mapping('output')

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################
        # TCDM Interconnect
        if terapool:
            #Sub group master output -> Sub group slave input
            for ini in range(0, nb_sub_groups_per_group):
                for tgt in range(0, nb_sub_groups_per_group):
                    if (ini != tgt):
                        for tile in range(0, nb_tiles_per_sub_group):
                            debug_router=router.Router(self, 'debug_router_ini%d_tgt%d_tile%d' % (ini, tgt, tile))
                            debug_router.add_mapping("output")
                            self.bind(self.sub_group_list[ini], f'sub_grp_remt{ini^tgt}_tile{tile}_master_out', debug_router, 'input')
                            self.bind(debug_router, 'output', self.sub_group_list[tgt], f'sub_grp_remt{ini^tgt}_tile{tile}_slave_in')

            #Tile remote master -> Group remote interleavers
            for port in range(0, nb_remote_group_ports):
                for i in range(0, nb_sub_groups_per_group):
                    for j in range(0, nb_tiles_per_sub_group):
                        self.bind(self.sub_group_list[i], f'grp_remt{port}_tile{j}_master_out', group_remote_master_interleavers[port], 'in_%d' % (j + i * nb_tiles_per_sub_group))

            #Group remote interleavers -> Group remote routers
            for port in range(0, nb_remote_group_ports):
                for i in range(0, nb_sub_groups_per_group):
                    for j in range(0, nb_tiles_per_sub_group):
                        self.bind(group_remote_master_interleavers[port], 'out_%d' % (j + i * nb_tiles_per_sub_group), group_remote_out_interfaces[port][i][j], 'input')
        else:
            #Tile local master -> Group local interconnect
            for i in range(0, nb_tiles_per_group):
                self.bind(self.tile_list[i], 'loc_remt_master_out', group_local_interleaver, 'in_%d' % i)

            #Group local interconnect -> Tile local slave
            for i in range(0, nb_tiles_per_group):
                self.bind(group_local_interleaver, 'out_%d' % i, self.tile_list[i], 'loc_remt_slave_in')

            #Tile remote master -> Group remote interleavers
            for port in range(0, nb_remote_ports):
                for i in range(0, nb_tiles_per_group):
                    self.bind(self.tile_list[i], f'grp_remt{port}_master_out', group_remote_master_interleavers[port], 'in_%d' % i)

            #Group remote interleavers -> Group remote routers
            for port in range(0, nb_remote_ports):
                for i in range(0, nb_tiles_per_group):
                    self.bind(group_remote_master_interleavers[port], 'out_%d' % i, group_remote_out_interfaces[port][i], 'input')

        # AXI Interconnect, does not exist on Terapool
        if not terapool:
            # Tile axi port -> axi interconnect
            for i in range(0, nb_tiles_per_group):
                self.bind(self.tile_list[i], 'axi_out', axi_ico, 'input')

        # AXI Interface
        if terapool:
            for i in range(0, nb_sub_groups_per_group):
                self.bind(self.sub_group_list[i], 'axi_out', axi_itf[i], 'input')
        else:
            self.bind(axi_ico, 'output', axi_itf, 'input')

        # DMA network(virtual, to emulate multiple backends)
        self.bind(dma_tcdm_itf, 'output', dma_tcdm_interleaver, 'input')
        if terapool:
            for i in range(0, nb_sub_groups_per_group):
                self.bind(dma_tcdm_interleaver, f'out_{i}', self.sub_group_list[i], 'dma_tcdm')
        else:
            for i in range(0, nb_tiles_per_group):
                self.bind(dma_tcdm_interleaver, f'out_{i}', self.tile_list[i], 'dma_tcdm')

        if terapool:
            self.bind(dma_axi_itf, 'output', dma_axi_interleaver, 'in_0')
            for i in range(0, nb_sub_groups_per_group):
                self.bind(dma_axi_interleaver, f'out_{i}', self.sub_group_list[i], 'dma_axi')
        else:
            self.bind(dma_axi_itf, 'output', axi_ico, 'input')

        # Loader
        if terapool:
            #Group loader -> Sub group loader
            for i in range(0, nb_sub_groups_per_group):
                self.bind(self, 'loader_start', self.sub_group_list[i], 'loader_start')
                self.bind(self, 'loader_entry', self.sub_group_list[i], 'loader_entry')
        else:
            #Group loader -> Tile loader
            for i in range(0, nb_tiles_per_group):
                self.bind(self, 'loader_start', self.tile_list[i], 'loader_start')
                self.bind(self, 'loader_entry', self.tile_list[i], 'loader_entry')


        ################################################################
        ##########               Group Interfaces             ##########
        ################################################################
        # TCDM interface
        if terapool:
            # Remote TCDM interface between tiles to the group
            for port in range(0, nb_remote_group_ports):
                for i in range(0, nb_sub_groups_per_group):
                    for j in range(0, nb_tiles_per_sub_group):
                        self.bind(self, f'grp_remt{port+1}_sg{i}_tile{j}_slave_in', self.sub_group_list[i], f'grp_remt{port}_tile{j}_slave_in')
                        self.bind(group_remote_out_interfaces[port][i][j], 'output', self, f'grp_remt{port+1}_sg{i}_tile{j}_master_out')

        else:
            # Remote TCDM interface between tiles to the group
            for port in range(0, nb_remote_ports):
                for i in range(0, nb_tiles_per_group):
                    self.bind(self, f'grp_remt{port+1}_tile{i}_slave_in', self.tile_list[i], f'grp_remt{port}_slave_in')
                    self.bind(group_remote_out_interfaces[port][i], 'output', self, f'grp_remt{port+1}_tile{i}_master_out')

        # Barrier
        if terapool:
            # Propagate the barrier signals from the tiles to the group boundary
            for i in range(0, nb_sub_groups_per_group):
                for j in range(0, nb_tiles_per_sub_group):
                    for k in range(0, nb_cores_per_tile):
                        self.bind(self, f'barrier_ack_{i*nb_tiles_per_sub_group*nb_cores_per_tile+j*nb_cores_per_tile+k}',
                                  self.sub_group_list[i], f'barrier_ack_{j*nb_cores_per_tile+k}')
        else:
            # Propagate the barrier signals from the tiles to the group boundary
            for i in range(0, nb_tiles_per_group):
                for j in range(0, nb_cores_per_tile):
                    self.bind(self, f'barrier_ack_{i*nb_cores_per_tile+j}', self.tile_list[i], f'barrier_ack_{j}')

        # L2 ro-cache configuration
        if terapool:
            for i in range(0, nb_sub_groups_per_group):
                self.bind(self, 'rocache_cfg', self.sub_group_list[i], 'rocache_cfg')
        else:
            self.bind(self, 'rocache_cfg', axi_ico, 'rocache_cfg')

        # AXI
        if terapool:
            for i in range(0, nb_sub_groups_per_group):
                self.bind(axi_itf[i], 'output', self, f'axi_out_{i}')
        else:
            self.bind(axi_itf, 'output', self, 'axi_out_0')

        # DMA
        self.bind(self, 'dma_tcdm', dma_tcdm_itf, 'input')
        self.bind(self, 'dma_axi', dma_axi_itf, 'input')
