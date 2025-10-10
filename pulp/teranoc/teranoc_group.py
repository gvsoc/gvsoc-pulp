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
# Discription: This file is the GVSoC configuration file for the TeraNoc Group.
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
#         Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)

import gvsoc.systree
import gvsoc.systree as st
import interco.router as router
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from interco.interleaver import Interleaver
import math
from pulp.teranoc.teranoc_tile import Tile
from pulp.teranoc.l1_interconnect.l1_noc_endpoint_router import L1NocEndpointRouter
from pulp.mempool.hierarchical_interco import Hierarchical_Interco

class Group(st.Component):

    def __init__(self, parent, name, parser, group_id_x: int=0, group_id_y: int=0, nb_cores_per_tile: int=4, nb_x_groups: int=4, nb_y_groups: int=4, total_cores: int=1024, nb_remote_ports_per_tile: int=2, bank_factor: int=4, axi_data_width: int=64):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters
        group_id = group_id_x * nb_y_groups + group_id_y
        nb_groups = nb_x_groups * nb_y_groups
        nb_tiles_per_group = int((total_cores/(nb_x_groups*nb_y_groups))/nb_cores_per_tile)
        nb_banks_per_tile = nb_cores_per_tile * bank_factor
        nb_remote_ports = nb_remote_ports_per_tile * nb_tiles_per_group

        ################################################################
        ##########              Design Components             ##########
        ################################################################
        # TIles
        self.tile_list = []
        for i in range(0, nb_tiles_per_group):
            self.tile_list.append(Tile(self,f'tile_{i}',parser=parser, tile_id=i, group_id_x=group_id_x, group_id_y=group_id_y, \
                nb_cores_per_tile=nb_cores_per_tile, nb_tiles_per_group=nb_tiles_per_group, nb_x_groups=nb_x_groups, nb_y_groups=nb_y_groups, \
                total_cores=total_cores, bank_factor=bank_factor, nb_remote_ports_per_tile=nb_remote_ports_per_tile, axi_data_width=axi_data_width))

        #Group local interconnect
        group_local_interleaver = Interleaver(self, 'group_local_interleaver', nb_slaves=nb_tiles_per_group, nb_masters=nb_tiles_per_group, 
            interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor)), offset_translation=False)

        # L1 NoC Request Router
        l1_noc_req_routers = []
        for i in range(0, nb_remote_ports_per_tile):
            l1_noc_req_routers.append(L1NocEndpointRouter(self, f'l1_noc_req_router_{i}', req_mode=True, nb_tiles_per_group=nb_tiles_per_group, num_banks_per_tile=nb_banks_per_tile, byte_offset=2))

        # L1 NoC Response Router
        l1_noc_resp_routers = []
        for i in range(0, nb_remote_ports_per_tile):
            l1_noc_resp_routers.append(L1NocEndpointRouter(self, f'l1_noc_resp_router_{i}', req_mode=False, nb_tiles_per_group=nb_tiles_per_group, num_banks_per_tile=nb_banks_per_tile, byte_offset=2))

        # DMA network(virtual, to emulate multiple backends)
        # DMA TCDM Interface
        dma_tcdm_itf = router.Router(self, f'dma_tcdm_itf', bandwidth=axi_data_width)
        dma_tcdm_itf.add_mapping('output')

        # DMA TCDM Interleaver
        dma_tcdm_interleaver = DmaInterleaver(self, f'dma_tcdm_interleaver', nb_master_ports=1, nb_banks=nb_tiles_per_group, bank_width=nb_banks_per_tile*4)

        # DMA AXI Interface
        dma_axi_itf = router.Router(self, f'dma_axi_itf', bandwidth=axi_data_width)
        dma_axi_itf.add_mapping('output')

        # Group-level AXI Interconnect
        # L2 cache rules
        l2_cache_rules = []
        l2_cache_rules.append((0x80000000, 0x80001000))
        l2_cache_rules.append((0xA0000000, 0xA0001000))
        l2_cache_rules.append((0x00000008, 0x0000000C))
        l2_cache_rules.append((0x0000000C, 0x00000010))
        # AXI Interconnect
        axi_ico = Hierarchical_Interco(self, 'axi_ico', enable_cache=True, cache_rules=l2_cache_rules, bandwidth=axi_data_width)

        # AXI Interface
        axi_itf = router.Router(self, 'axi_itf', bandwidth=axi_data_width, latency=2)
        axi_itf.add_mapping('output')

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################
        #Tile local master -> Group local interconnect
        for i in range(0, nb_tiles_per_group):
            self.bind(self.tile_list[i], 'loc_remt_master_out', group_local_interleaver, 'in_%d' % i)

        #Group local interconnect -> Tile local slave
        for i in range(0, nb_tiles_per_group):
            self.bind(group_local_interleaver, 'out_%d' % i, self.tile_list[i], 'loc_remt_slave_in')

        # L1 noc request router -> Tile l1 noc request slave
        for i in range(0, nb_tiles_per_group):
            for j in range(0, nb_remote_ports_per_tile):
                self.bind(l1_noc_req_routers[j], f'output_{i}', self.tile_list[i], f'l1_noc_req_slv_{j}')

        # L1 noc response router -> Tile l1 noc response master
        for i in range(0, nb_tiles_per_group):
            for j in range(0, nb_remote_ports_per_tile):
                self.bind(l1_noc_resp_routers[j], f'output_{i}', self.tile_list[i], f'l1_noc_resp_mst_{j}')

        # AXI Interconnect
        # Tile axi port -> axi interconnect
        for i in range(0, nb_tiles_per_group):
            self.bind(self.tile_list[i], 'axi_out', axi_ico, 'input')

        # AXI Interface
        self.bind(axi_ico, 'output', axi_itf, 'input')

        # DMA network(virtual, to emulate multiple backends)
        self.bind(dma_tcdm_itf, 'output', dma_tcdm_interleaver, 'input')
        for i in range(0, nb_tiles_per_group):
            self.bind(dma_tcdm_interleaver, f'out_{i}', self.tile_list[i], 'dma_tcdm')

        self.bind(dma_axi_itf, 'output', axi_ico, 'input')

        # Loader
        #Group loader -> Tile loader
        for i in range(0, nb_tiles_per_group):
            self.bind(self, 'loader_start', self.tile_list[i], 'loader_start')
            self.bind(self, 'loader_entry', self.tile_list[i], 'loader_entry')

        ################################################################
        ##########               Group Interfaces             ##########
        ################################################################
        # Tile l1 noc request master -> Group l1 noc request master
        for i in range(0, nb_tiles_per_group):
            for port in range(0, nb_remote_ports_per_tile):
                self.bind(self.tile_list[i], f'l1_noc_req_mst_{port}', self, f'l1_noc_req_mst_{i*nb_remote_ports_per_tile+port}')

        # Tile l1 noc response slave -> Group l1 noc response slave
        for i in range(0, nb_tiles_per_group):
            for port in range(0, nb_remote_ports_per_tile):
                self.bind(self.tile_list[i], f'l1_noc_resp_slv_{port}', self, f'l1_noc_resp_slv_{i*nb_remote_ports_per_tile+port}')

        # Group l1 noc request slave -> l1 noc request router
        for i in range(0, nb_tiles_per_group):
            for port in range(0, nb_remote_ports_per_tile):
                    self.bind(self, f'l1_noc_req_slv_{i*nb_remote_ports_per_tile+port}', l1_noc_req_routers[port], f'input_{i}')

        # Group l1 noc response master -> l1 noc response router
        for i in range(0, nb_tiles_per_group):
            for port in range(0, nb_remote_ports_per_tile):
                    self.bind(self, f'l1_noc_resp_mst_{i*nb_remote_ports_per_tile+port}', l1_noc_resp_routers[port], f'input_{i}')

        # Barrier
        # Propagate the barrier signals from the tiles to the group boundary
        for i in range(0, nb_tiles_per_group):
            for j in range(0, nb_cores_per_tile):
                self.bind(self, f'barrier_ack_{i*nb_cores_per_tile+j}', self.tile_list[i], f'barrier_ack_{j}')

        # L2 ro-cache configuration
        self.bind(self, 'rocache_cfg', axi_ico, 'rocache_cfg')

        # AXI
        self.bind(axi_itf, 'output', self, 'axi_out_0')

        # DMA
        self.bind(self, 'dma_tcdm', dma_tcdm_itf, 'input')
        self.bind(self, 'dma_axi', dma_axi_itf, 'input')

    def i_GROUP_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'grp_remt{port}_slave_in', signature='io')

    def o_GROUP_INPUT(self, port: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'grp_remt{port}_slave_in', itf, signature='io', composite_bind=True)

    def i_GROUP_OUTPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'grp_remt{port}_master_out', signature='io')

    def o_GROUP_OUTPUT(self, port: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'grp_remt{port}_master_out', itf, signature='io')