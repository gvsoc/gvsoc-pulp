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

import pulp.snitch.snitch_core as iss
from pulp.mempool.hierarchical_cache import Hierarchical_cache
import pulp.teranoc.l1_subsystem as l1_subsystem
import interco.router as router
import gvsoc.systree as st
from pulp.snitch.sequencer import Sequencer
from pulp.mempool.l1_address_scrambler import L1_AddressScrambler
from pulp.teranoc.l1_interconnect.l1_noc_itf import L1_NocItf

class Tile(st.Component):

    def __init__(self, parent, name, parser, tile_id: int=0, group_id_x: int=0, group_id_y: int=0, nb_cores_per_tile: int=4, nb_tiles_per_group: int=16, nb_x_groups: int=4, nb_y_groups: int=4, total_cores: int= 256, bank_factor: int=4, nb_remote_ports_per_tile: int=2, axi_data_width: int=64):
        super().__init__(parent, name)

        [args, __] = parser.parse_known_args()

        # Set it to true to swtich to snitch new fast model
        fast_model = True

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters 
        group_id = group_id_x * nb_y_groups + group_id_y
        nb_groups = nb_x_groups * nb_y_groups
        # global_tile_id = tile_id + group_id * nb_tiles_per_group
        Xfrep = 0
        # stack_size_per_tile = 0x800
        mem_size = nb_cores_per_tile * bank_factor * 1024

        # Snitch core complex
        self.int_cores = []
        self.fp_cores = []
        bus_watchpoints = []
        if Xfrep:
            fpu_sequencers = []

        ################################################################
        ##########              Design Components             ##########
        ################################################################

        # Snitch TCDM (L1 subsystem)
        l1 = l1_subsystem.L1_subsystem(self, 'l1', \
                                        tile_id=tile_id, group_id=group_id, nb_tiles_per_group=nb_tiles_per_group, \
                                        nb_groups=nb_groups, nb_remote_local_masters=1, \
                                        nb_remote_group_masters=nb_remote_ports_per_tile, nb_pe=nb_cores_per_tile, \
                                        size=mem_size, bandwidth=4, nb_banks_per_tile=nb_cores_per_tile*bank_factor, \
                                        axi_data_width=axi_data_width)

        # L1 NoC Interface
        l1_noc_itf = L1_NocItf(self, 'l1_noc_itf', nb_req_ports=nb_remote_ports_per_tile, nb_resp_ports=nb_remote_ports_per_tile, \
                                    tile_id=tile_id, group_id_x=group_id_x, group_id_y=group_id_y, nb_x_groups=nb_x_groups, nb_y_groups=nb_y_groups, \
                                    byte_offset=2, num_tiles_per_group=nb_tiles_per_group, num_banks_per_tile=nb_cores_per_tile*bank_factor)

        # Shared icache
        icache = Hierarchical_cache(self, 'shared_icache', nb_cores=nb_cores_per_tile)

        # Address Scrambler
        l1_addr_scrambler_list = []
        for i in range(0, nb_cores_per_tile):
            l1_addr_scrambler_list.append(L1_AddressScrambler(self, f'addr_scrambler{i}', \
                                                       bypass=False, num_tiles=int(total_cores/nb_cores_per_tile), \
                                                       seq_mem_size_per_tile=512*nb_cores_per_tile, byte_offset=2, \
                                                       num_banks_per_tile=nb_cores_per_tile*bank_factor))

        # Route
        ico_list=[]
        for i in range(0, nb_cores_per_tile):
            ico_list.append(router.Router(self, 'ico%d' % i, bandwidth=4, latency=0))
        axi_ico = router.Router(self, 'axi_ico', bandwidth=axi_data_width, latency=1)
        axi_ico.add_mapping('output')

        # Core Complex
        for core_id in range(0, nb_cores_per_tile):
            if fast_model:
                self.int_cores.append(iss.SnitchFast(self, f'pe{core_id}', isa="rv32imaf",
                    core_id=group_id*nb_tiles_per_group*nb_cores_per_tile+tile_id*nb_cores_per_tile+core_id,
                    htif=False, pulp_v2=True, wakeup_counter=True, nb_outstanding=8
                ))
            else:
                self.int_cores.append(iss.Snitch(self, f'pe{core_id}', isa="rv32imaf", htif=False, \
                    core_id=group_id*nb_tiles_per_group*nb_cores_per_tile+tile_id*nb_cores_per_tile+core_id,
                    pulp_v2=True))
                self.fp_cores.append(iss.Snitch_fp_ss(self, f'fp_ss{core_id}', isa="rv32imaf", htif=False, \
                    core_id=group_id*nb_tiles_per_group*nb_cores_per_tile+tile_id*nb_cores_per_tile+core_id,
                    pulp_v2=True))
                if Xfrep:
                    fpu_sequencers.append(Sequencer(self, f'fpu_sequencer{core_id}', latency=0))

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################

        ##########################################################################
        #                        |--> stack_ico --> stack_mem                    #
        # Core --> ico router -->|--> L1 submodule --> Remote TCDM interfaces    #
        #                        |--> AXI router --> ROM, CSR, L2 Memory, Dummy  #
        ##########################################################################

        # ICO --> L1 TCDM
        for i in range(0, nb_cores_per_tile):
            ico_list[i].add_mapping('l1', base=0x00000000, remove_offset=0x00000000, size=total_cores * bank_factor * 1024)
            self.bind(ico_list[i], 'l1', l1, f'pe_in{i}')

        # L1 TCDM --> Remote TCDM interfaces
        self.bind(self, 'loc_remt_slave_in', l1, 'remote_local_in0')
        self.bind(l1, 'remote_local_out0', self, 'loc_remt_master_out')

        for i in range(0, nb_remote_ports_per_tile):
            self.bind(l1_noc_itf, f'noc_req_mst_{i}', self, f'l1_noc_req_mst_{i}')
            self.bind(self, f'l1_noc_resp_mst_{i}', l1_noc_itf, f'noc_resp_mst_{i}')
            self.bind(self, f'l1_noc_req_slv_{i}', l1_noc_itf, f'noc_req_slv_{i}')
            self.bind(l1_noc_itf, f'noc_resp_slv_{i}', self, f'l1_noc_resp_slv_{i}')

        for i in range(0, nb_remote_ports_per_tile):
            self.bind(l1_noc_itf, f'tcdm_req_mst_{i}', l1, f'remote_group_in{i}')
            self.bind(l1, f'remote_group_out{i}', l1_noc_itf, f'core_req_slv_{i}')

        self.bind(self, 'dma_tcdm', l1, 'dma')

        # ICO -> AXI -> L2 Memory
        for i in range(0, nb_cores_per_tile):
            # Add default mapping for the others
            ico_list[i].add_mapping('axi', latency=4)
            self.bind(ico_list[i], 'axi', axi_ico, 'input')

        ###########################################################
        #                                       |--> ROM          #
        # Core -->|--icache--> |-->AXI router-->|--> CSR          #
        #                                       |--> L2 Memory    #
        #                                       |--> Dummy Memory #
        ###########################################################

        # Icache -> AXI
        self.bind(icache, 'refill', axi_ico, 'input')
        
        # AXI -> Remote AXI port
        self.bind(axi_ico, 'output', self, 'axi_out')

        # Sync barrier
        for core_id in range(0, nb_cores_per_tile):
            self.bind(self, f'barrier_ack_{core_id}', self.int_cores[core_id], 'barrier_ack')

        # Core Interconnections
        for core_id in range(0, nb_cores_per_tile):
            # Icache
            self.bind(self.int_cores[core_id], 'flush_cache_req', icache, 'flush')
            self.bind(icache, 'flush_ack', self.int_cores[core_id], 'flush_cache_ack')

            # Snitch integer cores
            self.bind(self.int_cores[core_id], 'data', l1_addr_scrambler_list[core_id], 'input')
            self.bind(self.int_cores[core_id], 'fetch', icache, 'input_%d' % core_id)
            self.bind(self, 'loader_start', self.int_cores[core_id], 'fetchen')
            self.bind(self, 'loader_entry', self.int_cores[core_id], 'bootaddr')

            if not fast_model:
                # Snitch fp subsystems
                # Pay attention to interactions and bandwidth between subsystem and tohost.
                self.bind(self.fp_cores[core_id], 'data', l1_addr_scrambler_list[core_id], 'input')
                # FP subsystem doesn't fetch instructions from core->ico->memory, but from integer cores acc_req.
                self.bind(self, 'loader_start', self.fp_cores[core_id], 'fetchen')
                self.bind(self, 'loader_entry', self.fp_cores[core_id], 'bootaddr')

            # Scrambler
            self.bind(l1_addr_scrambler_list[core_id], 'output', ico_list[core_id], 'input')

            # Use WireMaster & WireSlave
            # Add fpu sequence buffer in between int core and fp core to issue instructions
            if not fast_model:
                if Xfrep:
                    self.bind(self.int_cores[core_id], 'acc_req', fpu_sequencers[core_id], 'input')
                    self.bind(fpu_sequencers[core_id], 'output', self.fp_cores[core_id], 'acc_req')
                    self.bind(self.int_cores[core_id], 'acc_req_ready', fpu_sequencers[core_id], 'acc_req_ready')
                    self.bind(fpu_sequencers[core_id], 'acc_req_ready_o', self.fp_cores[core_id], 'acc_req_ready')
                else:
                    # Comment out if we want to add sequencer
                    self.bind(self.int_cores[core_id], 'acc_req', self.fp_cores[core_id], 'acc_req')
                    self.bind(self.int_cores[core_id], 'acc_req_ready', self.fp_cores[core_id], 'acc_req_ready')

                self.bind(self.fp_cores[core_id], 'acc_rsp', self.int_cores[core_id], 'acc_rsp')
