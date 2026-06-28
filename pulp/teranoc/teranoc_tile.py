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
# Discription: This file is the GVSoC configuration file for the TeraNoc Tile.
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
#         Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)

from pulp.cpu.iss.snitch_mempool import SnitchMempool, SnitchMempoolConfig
from pulp.mempool.hierarchical_cache import Hierarchical_cache
from pulp.mempool.xbar.mempool_xbar import MempoolXbar
from pulp.mempool.l1_interconnect.l1_remote_itf import L1_RemoteItf
import pulp.teranoc.l1_subsystem as l1_subsystem
import interco.router as router
import gvsoc.systree as st
from pulp.mempool.l1_interconnect.l1_address_scrambler import L1AddressScrambler
from pulp.teranoc.l1_interconnect.l1_noc_itf import L1_NocItf

class TeranocTile(st.Component):

    def __init__(self, parent, name, parser, arch, tile_id: int=0, group_id_x: int=0, group_id_y: int=0):
        super().__init__(parent, name)

        [args, __] = parser.parse_known_args()

        # Local one-shot constants for this tile.
        group_id = group_id_x * arch.nb_y_groups + group_id_y

        # Snitch core complex
        self.int_cores = []
        bus_watchpoints = []

        ################################################################
        ##########              Design Components             ##########
        ################################################################

        # Snitch TCDM (L1 subsystem). Local ports take all in-tile masters
        # (today only Snitch). Remote ports are wired below: port 0 is the
        # intra-group neighbor; ports 1..N go to the NoC.
        l1 = l1_subsystem.L1_subsystem(self, 'l1',
            tile_id=tile_id, group_id=group_id,
            nb_tiles_per_group=arch.nb_tiles_per_group, nb_groups=arch.nb_groups,
            nb_local_ports=arch.nb_local_ports,
            nb_remote_ports=arch.nb_remote_ports,
            size=arch.l1_per_tile_bytes, bandwidth=arch.l1_bank_width,
            nb_banks_per_tile=arch.nb_banks_per_tile,
            axi_data_width=arch.axi_data_width)

        # L1 NoC Interface
        l1_noc_itf = L1_NocItf(self, 'l1_noc_itf', nb_req_ports=arch.nb_remote_ports_per_tile, nb_resp_ports=arch.nb_remote_ports_per_tile, \
                                    tile_id=tile_id, group_id_x=group_id_x, group_id_y=group_id_y, nb_x_groups=arch.nb_x_groups, nb_y_groups=arch.nb_y_groups, \
                                    byte_offset=2, num_tiles_per_group=arch.nb_tiles_per_group, num_banks_per_tile=arch.nb_banks_per_tile)

        # Shared icache
        icache = Hierarchical_cache(self, 'shared_icache', nb_cores=arch.nb_snitch_per_tile, synchronous=False)

        # Address Scrambler
        l1_addr_scrambler_list = []
        for i in range(0, arch.nb_snitch_per_tile):
            l1_addr_scrambler_list.append(L1AddressScrambler(self, f'addr_scrambler{i}',
                                                       bypass=False, num_tiles=arch.nb_tiles_total,
                                                       seq_mem_size_per_tile=512*arch.nb_snitch_per_tile, byte_offset=2,
                                                       num_banks_per_tile=arch.nb_banks_per_tile))

        # Route
        ico_list=[]
        for i in range(0, arch.nb_snitch_per_tile):
            ico_list.append(router.Router(self, 'ico%d' % i, bandwidth=4, latency=0))

        core_axi_itf = L1_RemoteItf(self, 'core_axi_itf', req_latency=1, resp_latency=1, bandwidth=4, shared_rw_bandwidth=True, synchronous=False)
        cache_axi_itf = L1_RemoteItf(self, 'cache_axi_itf', req_latency=0, resp_latency=1, bandwidth=arch.axi_data_width, shared_rw_bandwidth=False, synchronous=False)
        axi_ico = router.Router(self, 'axi_ico', latency=1, bandwidth=arch.axi_data_width, synchronous=False, shared_rw_bandwidth=False, max_input_pending_size=arch.axi_data_width)
        axi_ico.add_mapping('output')
        _ = axi_ico.i_INPUT(1)

        # Snitch core complex
        for core_id in range(0, arch.nb_snitch_per_tile):
            hart_id = (group_id * arch.nb_tiles_per_group * arch.nb_snitch_per_tile
                       + tile_id * arch.nb_snitch_per_tile + core_id)
            # SnitchMempool: barrier CSR + wake counter, no vector unit.
            core = SnitchMempool(self, f'pe{core_id}',
                config=SnitchMempoolConfig(isa="rv32imaf", hart_id=hart_id,
                    htif=False, fetch_enable=False, boot_addr=0,
                    nb_outstanding=8))
            self.int_cores.append(core)

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################

        ##########################################################################
        #                        |--> stack_ico --> stack_mem                    #
        # Core --> ico router -->|--> L1 submodule --> Remote TCDM interfaces    #
        #                        |--> AXI router --> ROM, CSR, L2 Memory, Dummy  #
        ##########################################################################

        # Snitch ICOs use local ports 0..nb_snitch-1.
        for i in range(0, arch.nb_snitch_per_tile):
            ico_list[i].add_mapping('l1', base=0x00000000, remove_offset=0x00000000, size=arch.l1_total_bytes)
            self.bind(ico_list[i], 'l1', l1, f'local_in_{i}')

        # Remote ports: index 0 is the intra-group neighbor; 1..N are the NoC.
        self.bind(self, 'loc_remt_slave_in', l1, 'remote_in_0')
        self.bind(l1, 'remote_out_0', self, 'loc_remt_master_out')

        for i in range(0, arch.nb_remote_ports_per_tile):
            self.bind(l1_noc_itf, f'noc_req_mst_{i}', self, f'l1_noc_req_mst_{i}')
            self.bind(self, f'l1_noc_resp_mst_{i}', l1_noc_itf, f'noc_resp_mst_{i}')
            self.bind(self, f'l1_noc_req_slv_{i}', l1_noc_itf, f'noc_req_slv_{i}')
            self.bind(l1_noc_itf, f'noc_resp_slv_{i}', self, f'l1_noc_resp_slv_{i}')

        for i in range(0, arch.nb_remote_ports_per_tile):
            self.bind(l1_noc_itf, f'tcdm_req_mst_{i}', l1, f'remote_in_{i + 1}')
            self.bind(l1, f'remote_out_{i + 1}', l1_noc_itf, f'core_req_slv_{i}')

        self.bind(self, 'dma_tcdm', l1, 'dma')

        # ICO -> AXI -> L2 Memory
        for i in range(0, arch.nb_snitch_per_tile):
            # Add default mapping for the others
            ico_list[i].add_mapping('axi')
            self.bind(ico_list[i], 'axi', core_axi_itf, 'input')
        self.bind(core_axi_itf, 'output', axi_ico, 'input')

        ###########################################################
        #                                       |--> ROM          #
        # Core -->|--icache--> |-->AXI router-->|--> CSR          #
        #                                       |--> L2 Memory    #
        #                                       |--> Dummy Memory #
        ###########################################################

        # Icache -> AXI
        self.bind(icache, 'refill', cache_axi_itf, 'input')
        self.bind(cache_axi_itf, 'output', axi_ico, 'input_1')

        # AXI -> Remote AXI port
        self.bind(axi_ico, 'output', self, 'axi_out')

        # Sync barrier
        for core_id in range(0, arch.nb_snitch_per_tile):
            self.bind(self, f'barrier_ack_{core_id}', self.int_cores[core_id], 'barrier_ack')

        # Core Interconnections
        for core_id in range(0, arch.nb_snitch_per_tile):
            # Icache
            self.bind(self.int_cores[core_id], 'flush_cache_req', icache, 'flush')
            self.bind(icache, 'flush_ack', self.int_cores[core_id], 'flush_cache_ack')

            # Snitch integer cores
            self.bind(self.int_cores[core_id], 'data', l1_addr_scrambler_list[core_id], 'input')
            self.bind(self.int_cores[core_id], 'fetch', icache, 'input_%d' % core_id)
            self.bind(self, 'loader_start', self.int_cores[core_id], 'fetchen')
            self.bind(self, 'loader_entry', self.int_cores[core_id], 'bootaddr')

            # Scrambler
            self.bind(l1_addr_scrambler_list[core_id], 'output', ico_list[core_id], 'input')
