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
#         Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)

import pulp.snitch.snitch_core as iss
from pulp.mempool.hierarchical_cache import Hierarchical_cache
import pulp.mempool.l1_subsystem as l1_subsystem
import interco.router as router
import gvsoc.systree as st
from pulp.snitch.sequencer import Sequencer
from pulp.mempool.l1_interconnect.l1_address_scrambler import L1AddressScrambler

from pulp.cpu.iss.spatz_config import SpatzConfig
from pulp.cpu.iss.spatz_mempool import SpatzMempool

class Tile(st.Component):

    def __init__(self, parent, name, parser, terapool: bool=False, async_l1_interco: bool=False, tile_id: int=0, sub_group_id: int=0, group_id: int=0, nb_cores_per_tile: int=4, nb_sub_groups_per_group: int=1, nb_groups: int=4, total_cores: int= 256, bank_factor: int=4, axi_data_width: int=64, terapool_group_latency: int=7, nb_fus_per_core: int=1):
        super().__init__(parent, name)

        [args, __] = parser.parse_known_args()

        # Set it to true to swtich to snitch new fast model
        fast_model = True
        use_spatz = nb_fus_per_core > 1

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters
        nb_remote_group_ports = nb_groups - 1
        nb_remote_sub_group_ports = nb_sub_groups_per_group - 1
        nb_tiles_per_sub_group = int((total_cores/nb_groups/nb_sub_groups_per_group)/nb_cores_per_tile)
        # global_tile_id = tile_id + group_id * nb_tiles_per_group
        Xfrep = 0
        # stack_size_per_tile = 0x800
        mem_size = nb_cores_per_tile * bank_factor * nb_fus_per_core * 1024

        # Snitch core complex
        self.int_cores = []
        self.fp_cores = []
        bus_watchpoints = []
        if Xfrep and not use_spatz:
            fpu_sequencers = []

        ################################################################
        ##########              Design Components             ##########
        ################################################################

        # Snitch TCDM (L1 subsystem)
        # nb_pe is the number of local master ports within the tile
        nb_pe = nb_cores_per_tile * (nb_fus_per_core + 1) if use_spatz else nb_cores_per_tile
        l1 = l1_subsystem.L1_subsystem(self, 'l1', terapool=terapool, \
                                        async_l1_interco=async_l1_interco, tile_id=tile_id, sub_group_id=sub_group_id, group_id=group_id, \
                                        nb_tiles_per_sub_group=nb_tiles_per_sub_group, nb_sub_groups_per_group=nb_sub_groups_per_group, \
                                        nb_groups=nb_groups, nb_remote_local_masters=1, nb_remote_group_masters=nb_remote_group_ports, \
                                        nb_remote_sub_group_masters=nb_remote_sub_group_ports, nb_pe=nb_pe, size=mem_size, \
                                        bandwidth=4, nb_banks_per_tile=nb_cores_per_tile*nb_fus_per_core*bank_factor, axi_data_width=axi_data_width, \
                                        terapool_group_latency=terapool_group_latency)
        # Shared icache
        icache = Hierarchical_cache(self, 'shared_icache', nb_cores=nb_cores_per_tile, nb_fus_per_core=nb_fus_per_core)

        # Address Scrambler
        l1_addr_scrambler_list = []
        for i in range(0, nb_cores_per_tile):
            l1_addr_scrambler_list.append(L1AddressScrambler(self, f'addr_scrambler{i}',
                                                       bypass=False, num_tiles=int(total_cores/nb_cores_per_tile),
                                                       seq_mem_size_per_tile=512*nb_cores_per_tile, byte_offset=2,
                                                       num_banks_per_tile=nb_cores_per_tile*nb_fus_per_core*bank_factor))

        # Route
        ico_list=[]
        for i in range(0, nb_pe):
            ico_list.append(router.Router(self, 'ico%d' % i, bandwidth=4, latency=0))
        axi_ico = router.Router(self, 'axi_ico', bandwidth=axi_data_width, latency=1)
        axi_ico.add_mapping('output')

        # Core Complex
        for core_id in range(0, nb_cores_per_tile):
            core_global_id = group_id*nb_sub_groups_per_group*nb_tiles_per_sub_group*nb_cores_per_tile+sub_group_id*nb_tiles_per_sub_group*nb_cores_per_tile+tile_id*nb_cores_per_tile+core_id

            if use_spatz:
                config = SpatzConfig(isa="rv32imafv", fetch_enable=False,
                    boot_addr=0, hart_id=core_global_id,
                    htif=False, nb_lanes=nb_fus_per_core, lane_width=4)
                self.int_cores.append(SpatzMempool(self, f'pe{core_id}', config=config))

            elif fast_model:
                self.int_cores.append(iss.SnitchFast(self, f'pe{core_id}', isa="rv32imaf",
                    core_id=core_global_id, htif=False, pulp_v2=True, wakeup_counter=True, nb_outstanding=8
                ))

            else:
                self.int_cores.append(iss.Snitch(self, f'pe{core_id}', isa="rv32imaf", htif=False, \
                    core_id=core_global_id, pulp_v2=True))
                self.fp_cores.append(iss.Snitch_fp_ss(self, f'fp_ss{core_id}', isa="rv32imaf", htif=False, \
                    core_id=core_global_id, pulp_v2=True))
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
        for i in range(0, nb_pe):
            ico_list[i].add_mapping('l1', base=0x00000000, remove_offset=0x00000000, size=total_cores * nb_fus_per_core * bank_factor * 1024)
            self.bind(ico_list[i], 'l1', l1, f'pe_in{i}')

        # L1 TCDM --> Remote TCDM interfaces
        self.bind(self, 'loc_remt_slave_in', l1, 'remote_local_in0')
        self.bind(l1, 'remote_local_out0', self, 'loc_remt_master_out')

        for i in range(0, nb_remote_sub_group_ports):
            self.bind(self, f'sub_grp_remt{i}_slave_in', l1, f'remote_sub_group_in{i}')
            self.bind(l1, f'remote_sub_group_out{i}', self, f'sub_grp_remt{i}_master_out')

        for i in range(0, nb_remote_group_ports):
            self.bind(self, f'grp_remt{i}_slave_in', l1, f'remote_group_in{i}')
            self.bind(l1, f'remote_group_out{i}', self, f'grp_remt{i}_master_out')

        self.bind(self, 'dma_tcdm', l1, 'dma')

        # ICO -> AXI -> L2 Memory
        for i in range(0, nb_cores_per_tile):
            # Add default mapping for the others
            core_id = i * (1 + nb_fus_per_core) if use_spatz else i
            ico_list[core_id].add_mapping('axi', latency=4)
            self.bind(ico_list[core_id], 'axi', axi_ico, 'input')

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
        pe_id = 0
        for core_id in range(0, nb_cores_per_tile):
            # Icache
            self.bind(self.int_cores[core_id], 'flush_cache_req', icache, 'flush')
            self.bind(icache, 'flush_ack', self.int_cores[core_id], 'flush_cache_ack')

            # Snitch integer cores
            self.bind(self.int_cores[core_id], 'data', l1_addr_scrambler_list[pe_id], 'input')
            self.bind(self.int_cores[core_id], 'fetch', icache, 'input_%d' % core_id)
            self.bind(self, 'loader_start', self.int_cores[core_id], 'fetchen')
            self.bind(self, 'loader_entry', self.int_cores[core_id], 'bootaddr')

            if not use_spatz and not fast_model:
                # Snitch fp subsystems
                # Pay attention to interactions and bandwidth between subsystem and tohost.
                self.bind(self.fp_cores[core_id], 'data', l1_addr_scrambler_list[pe_id], 'input')
                # FP subsystem doesn't fetch instructions from core->ico->memory, but from integer cores acc_req.
                self.bind(self, 'loader_start', self.fp_cores[core_id], 'fetchen')
                self.bind(self, 'loader_entry', self.fp_cores[core_id], 'bootaddr')
            
            # Scrambler
            self.bind(l1_addr_scrambler_list[pe_id], 'output', ico_list[pe_id], 'input')
            pe_id += 1

            if use_spatz:
                for port in range(0, nb_fus_per_core):
                    self.bind(self.int_cores[core_id], f'vlsu_{port}', l1_addr_scrambler_list[pe_id], 'input')
                    self.bind(l1_addr_scrambler_list[pe_id], 'output', ico_list[pe_id], 'input')
                    pe_id += 1
            elif not fast_model:
                # Use WireMaster & WireSlave
                # Add fpu sequence buffer in between int core and fp core to issue instructions
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
