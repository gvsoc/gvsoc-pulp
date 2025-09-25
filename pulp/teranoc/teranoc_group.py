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
from pulp.teranoc.l1_noc_address_converter import L1NocAddressConverter
from pulp.mempool.hierarchical_interco import Hierarchical_Interco

class Group(st.Component):

    def __init__(self, parent, name, parser, group_id: int=0, nb_cores_per_tile: int=4, nb_x_groups: int=4, nb_y_groups: int=4, total_cores: int=1024, nb_remote_ports_per_tile: int=2, bank_factor: int=4, axi_data_width: int=64):
        super().__init__(parent, name)

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters
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
            self.tile_list.append(Tile(self,f'tile_{i}',parser=parser, tile_id=i, group_id=group_id, nb_cores_per_tile=nb_cores_per_tile,
                nb_tiles_per_group=nb_tiles_per_group, nb_groups=nb_groups, total_cores=total_cores, bank_factor=bank_factor,
                nb_remote_ports_per_tile=nb_remote_ports_per_tile, axi_data_width=axi_data_width))

        #Group local interconnect
        group_local_interleaver = Interleaver(self, 'group_local_interleaver', nb_slaves=nb_tiles_per_group, nb_masters=nb_tiles_per_group, 
            interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor)), offset_translation=False)

        #Group Remote Address Converter
        group_remote_output_address_converters = []
        for i in range(0, nb_remote_ports):
            group_remote_output_address_converters.append(L1NocAddressConverter(self, f'group_remote_output_address_converter_{i}', bypass=False, xbar_to_noc=True, byte_offset=2, bank_size=1024,
                                                            num_groups=nb_groups, num_tiles_per_group=nb_tiles_per_group, num_banks_per_tile=bank_factor*nb_cores_per_tile))

        group_remote_input_address_converters = []
        for i in range(0, nb_remote_ports):
            group_remote_input_address_converters.append(L1NocAddressConverter(self, f'group_remote_input_address_converter_{i}', bypass=False, xbar_to_noc=False, byte_offset=2, bank_size=1024,
                                                            num_groups=nb_groups, num_tiles_per_group=nb_tiles_per_group, num_banks_per_tile=bank_factor*nb_cores_per_tile))

        #Group Remote Slave Interconnect
        group_remote_slave_interleavers = []
        for i in range(0, nb_remote_ports_per_tile):
            group_remote_slave_interleavers.append(Interleaver(self, f'group_remote_slave_interleaver_{i}', nb_slaves=nb_tiles_per_group, nb_masters=nb_tiles_per_group, interleaving_bits=int(math.log2(4*nb_cores_per_tile*bank_factor)), offset_translation=False))

        group_remote_out_interfaces = []
        for i in range(0, nb_remote_ports):
            itf = router.Router(self, f'group_remote_out_itf{i}', latency=0, bandwidth=4, shared_rw_bandwidth=True)
            itf.add_mapping('output')
            group_remote_out_interfaces.append(itf)

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

        #Tile remote master -> Group remote output address converter
        for i in range(0, nb_tiles_per_group):
            for port in range(0, nb_remote_ports_per_tile):
                self.bind(self.tile_list[i], f'grp_remt{port}_master_out', group_remote_output_address_converters[i*nb_remote_ports_per_tile+port], 'input')

        #Group remote output address converter -> Group remote master
        for i in range(0, nb_remote_ports):
            self.bind(group_remote_output_address_converters[i], 'output', group_remote_out_interfaces[i], 'input')

        #Group remote input address converter -> Group remote slave interleaver
        for i in range(0, nb_tiles_per_group):
            for j in range(0, nb_remote_ports_per_tile):
                self.bind(group_remote_input_address_converters[i*nb_remote_ports_per_tile+j], 'output', group_remote_slave_interleavers[j], 'in_%d' % i)

        #Group remote slave interleaver -> Tile remote slave
        for i in range(0, nb_tiles_per_group):
            for j in range(0, nb_remote_ports_per_tile):
                self.bind(group_remote_slave_interleavers[j], f'out_%d' % i, self.tile_list[i], f'grp_remt{j}_slave_in')

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
        # Remote TCDM interface between tiles to the group
        for i in range(0, nb_remote_ports):
            self.bind(self, f'grp_remt{i}_slave_in', group_remote_input_address_converters[i], 'input')
            self.bind(group_remote_out_interfaces[i], 'output', self, f'grp_remt{i}_master_out')            

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