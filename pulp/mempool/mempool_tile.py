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

from pulp.mempool.hierarchical_cache import Hierarchical_cache
import pulp.mempool.l1_subsystem as l1_subsystem
import interco.router as router
import gvsoc.systree as st
from pulp.mempool.l1_interconnect.l1_address_scrambler import L1AddressScrambler
from pulp.cpu.iss.snitch_mempool import SnitchMempoolConfig, SnitchMempool

from pulp.light_redmule.light_redmule import LightRedmule
from pulp.light_opope.light_opope import LightOpope
from utils.common_cells import Or
from pulp.mempool.irq_hold import IrqHold
from pulp.mempool.redmule_configurations import RedmuleParam

class Tile(st.Component):

    def __init__(self, parent, name, parser, redmule_config: RedmuleParam=None, has_redmule: bool=False, tensorpool: bool=False, terapool: bool=False, async_l1_interco: bool=False, tile_id: int=0, sub_group_id: int=0, group_id: int=0, nb_cores_per_tile: int=4, nb_sub_groups_per_group: int=1, nb_groups: int=4, total_cores: int= 256, bank_factor: int=4, bank_size: int=1024, axi_data_width: int=64, terapool_group_latency: int=7, nb_fus_per_core: int=1, redmule_bandwidth: int=64):
        super().__init__(parent, name)

        # A RedMulE can only be instantiated on the tensorpool configuration, which is the only
        # one providing the dedicated wide RedMulE L1 path.
        assert not has_redmule or tensorpool, "has_redmule requires the tensorpool configuration"

        if has_redmule and redmule_config.engine == 'opope':
            # Outer product engine, in place of RedMulE (same ports)
            redmule = LightOpope(self, f'tile-{tile_id}-redmule',
                                    tcdm_bank_width     = 4,
                                    tcdm_bank_number    = redmule_config.tcdm_bank_number,
                                    p                   = redmule_config.opope_p,
                                    elem_size           = 2,
                                    mac_latency         = redmule_config.opope_mac_latency,
                                    input_slots         = redmule_config.opope_input_slots,
                                    stream_loads        = redmule_config.stream_loads,
                                    prefetch_depth      = redmule_config.opope_prefetch_depth,
                                    queue_depth         = redmule_config.queue_depth
                                    )
        elif has_redmule:
            # REDMULE
            #redmule = LightRedmule(self, 'redmule')
            redmule = LightRedmule(self, f'tile-{tile_id}-redmule',
                                    tcdm_bank_width     = 4,
                                    tcdm_bank_number    = redmule_config.tcdm_bank_number,
                                    elem_size           = 2,
                                    ce_height           = redmule_config.redmule_height,
                                    ce_width            = redmule_config.redmule_width,
                                    ce_pipe             = redmule_config.redmule_regs,
                                    queue_depth         = redmule_config.queue_depth,
                                    stream_loads        = redmule_config.stream_loads,
                                    row_refill_cyc      = redmule_config.row_refill_cyc,
                                    outstanding         = True
                                    )

        [args, __] = parser.parse_known_args()
        use_spatz = nb_fus_per_core > 1

        ################################################################
        ##########               Design Variables             ##########
        ################################################################


        # Hardware parameters
        nb_remote_group_ports = nb_groups - 1
        nb_remote_sub_group_ports = nb_sub_groups_per_group - 1
        nb_tiles_per_sub_group = int((total_cores/nb_groups/nb_sub_groups_per_group)/nb_cores_per_tile)
        # global_tile_id = tile_id + group_id * nb_tiles_per_group
        # stack_size_per_tile = 0x800
        mem_size = nb_cores_per_tile * bank_factor * nb_fus_per_core * bank_size

        # core complex
        self.cores = []

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
                                        terapool_group_latency=terapool_group_latency, \
                                        tensorpool=tensorpool, redmule_bandwidth=redmule_bandwidth)
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
                config = SnitchMempoolConfig(isa="rv32imafv", fetch_enable=False,
                    boot_addr=0, hart_id=core_global_id, htif=False, nb_outstanding=8, 
                    vector=True, nb_lanes=nb_fus_per_core, lane_width=4)
                self.cores.append(SnitchMempool(self, f'pe{core_id}', config=config))

            else:
                config = SnitchMempoolConfig(isa="rv32imaf", fetch_enable=False,
                    boot_addr=0, hart_id=core_global_id, htif=False, nb_outstanding=8, 
                    vector=False)
                self.cores.append(SnitchMempool(self, f'pe{core_id}', config=config))

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
            ico_list[i].add_mapping('l1', base=0x00000000, remove_offset=0x00000000, size=total_cores * nb_fus_per_core * bank_factor * bank_size)
            self.bind(ico_list[i], 'l1', l1, f'pe_in{i}')

        # Core 0 --> Redmule
        if has_redmule:
            ico_list[0].add_mapping('redmule_config', base=0x40020000, remove_offset=0x40020000, size=0x200)
            self.bind(ico_list[0], 'redmule_config', redmule, 'input')
            #input connections to all 16 different tcdm connections of redmule
            self.bind(redmule, 'tcdm', l1, 'RedMulE_input')


        # L1 TCDM --> Remote TCDM interfaces
        self.bind(self, 'loc_remt_slave_in', l1, 'remote_local_in0')
        self.bind(l1, 'remote_local_out0', self, 'loc_remt_master_out')

        for i in range(0, nb_remote_sub_group_ports):
            self.bind(self, f'sub_grp_remt{i}_slave_in', l1, f'remote_sub_group_in{i}')
            self.bind(l1, f'remote_sub_group_out{i}', self, f'sub_grp_remt{i}_master_out')

        for i in range(0, nb_remote_group_ports):
            self.bind(self, f'grp_remt{i}_slave_in', l1, f'remote_group_in{i}')
            self.bind(l1, f'remote_group_out{i}', self, f'grp_remt{i}_master_out')

        # Dedicated wide RedMulE remote channel (mirrors the ports above, redmule_-prefixed)
        if tensorpool:
            self.bind(self, 'redmule_loc_remt_slave_in', l1, 'redmule_remote_local_in0')
            self.bind(l1, 'redmule_remote_local_out0', self, 'redmule_loc_remt_master_out')

            for i in range(0, nb_remote_sub_group_ports):
                self.bind(self, f'redmule_sub_grp_remt{i}_slave_in', l1, f'redmule_remote_sub_group_in{i}')
                self.bind(l1, f'redmule_remote_sub_group_out{i}', self, f'redmule_sub_grp_remt{i}_master_out')

            for i in range(0, nb_remote_group_ports):
                self.bind(self, f'redmule_grp_remt{i}_slave_in', l1, f'redmule_remote_group_in{i}')
                self.bind(l1, f'redmule_remote_group_out{i}', self, f'redmule_grp_remt{i}_master_out')

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

        # Sync barrier, change to leave out core_0 to accomodate the possibility of a redmule.
        for core_id in range(1, nb_cores_per_tile):#not including core 0 anymore
            self.bind(self, f'barrier_ack_{core_id}', self.cores[core_id], 'barrier_ack')

        if has_redmule:
            redmule_interrupt = Or(self, 'barrier_ack_OR_IRQ', nb_input=2) #only two inputs are either barr_ack or IRQ from redmule
            #sync_barrier --> redmule_interrupt
            self.bind(self, 'barrier_ack_0', redmule_interrupt, 'input_0')
            #redmule IRQ --> rising-edge hold (drops the done falling edge that would inject a spurious
            #wakeup and desync core 0 from the barrier) --> redmule_interrupt
            redmule_done_hold = IrqHold(self, 'redmule_done_hold')
            self.bind(redmule, 'done_irq', redmule_done_hold, 'input')
            self.bind(redmule_done_hold, 'output', redmule_interrupt, 'input_1')
            #redmule_interrupt --> core 0
            self.bind(redmule_interrupt, 'output', self.cores[0], 'barrier_ack')
        else:
            self.bind(self, f'barrier_ack_0', self.cores[0], 'barrier_ack')


        # Core Interconnections
        pe_id = 0
        for core_id in range(0, nb_cores_per_tile):
            # Icache
            self.bind(self.cores[core_id], 'flush_cache_req', icache, 'flush')
            self.bind(icache, 'flush_ack', self.cores[core_id], 'flush_cache_ack')

            # Snitch integer cores
            self.bind(self.cores[core_id], 'data', l1_addr_scrambler_list[pe_id], 'input')
            self.bind(self.cores[core_id], 'fetch', icache, 'input_%d' % core_id)
            self.bind(self, 'loader_start', self.cores[core_id], 'fetchen')
            self.bind(self, 'loader_entry', self.cores[core_id], 'bootaddr')
            
            # Scrambler
            self.bind(l1_addr_scrambler_list[pe_id], 'output', ico_list[pe_id], 'input')
            pe_id += 1

            if use_spatz:
                for port in range(0, nb_fus_per_core):
                    self.bind(self.cores[core_id], f'vlsu_{port}', l1_addr_scrambler_list[pe_id], 'input')
                    self.bind(l1_addr_scrambler_list[pe_id], 'output', ico_list[pe_id], 'input')
                    pe_id += 1
