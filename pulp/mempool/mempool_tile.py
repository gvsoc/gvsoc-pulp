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

import gvsoc.runner
import pulp.snitch.snitch_core as iss
from memory.memory import Memory
from pulp.snitch.hierarchical_cache import Hierarchical_cache
from vp.clock_domain import Clock_domain
import pulp.mempool.l1_subsystem as l1_subsystem
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from pulp.idma.snitch_dma import SnitchDma
from interco.bus_watchpoint import Bus_watchpoint
from pulp.snitch.sequencer import Sequencer
from pulp.spatz.cluster_registers import Cluster_registers
from pulp.mempool.address_scrambler import AddressScrambler
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
import math

GAPY_TARGET = True

class Tile(st.Component):

    def __init__(self, parent, name, parser, terapool: bool=False, tile_id: int=0, sub_group_id: int=0, group_id: int=0, nb_cores_per_tile: int=4, nb_sub_groups_per_group: int=1, nb_groups: int=4, total_cores: int= 256, bank_factor: int=4, axi_data_width: int=64):
        super().__init__(parent, name)

        [args, __] = parser.parse_known_args()

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        # Set it to true to swtich to snitch new fast model
        fast_model = False

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
                                        terapool=terapool, tile_id=tile_id, sub_group_id=sub_group_id, group_id=group_id, \
                                        nb_tiles_per_sub_group=nb_tiles_per_sub_group, nb_sub_groups_per_group=nb_sub_groups_per_group, \
                                        nb_groups=nb_groups, nb_remote_local_masters=1, nb_remote_group_masters=nb_remote_group_ports, \
                                        nb_remote_sub_group_masters=nb_remote_sub_group_ports, nb_pe=nb_cores_per_tile, \
                                        size=mem_size, bandwidth=4,nb_banks_per_tile=nb_cores_per_tile*bank_factor)
        # Shared icache
        icache = Hierarchical_cache(self, 'shared_icache', nb_cores=nb_cores_per_tile, has_cc=0, l1_line_size_bits=7)

        # Address Scrambler
        addr_scrambler_list = []
        for i in range(0, nb_cores_per_tile):
            addr_scrambler_list.append(AddressScrambler(self, f'addr_scrambler{i}', \
                                                       bypass=False, num_tiles=int(total_cores/nb_cores_per_tile), \
                                                       seq_mem_size_per_tile=512, byte_offset=2, \
                                                       num_banks_per_tile=nb_cores_per_tile*bank_factor))

        # Route
        ico_list=[]
        for i in range(0, nb_cores_per_tile):
            ico_list.append(router.Router(self, 'ico%d' % i, bandwidth=4, latency=0))
        axi_ico = router.Router(self, 'axi_ico', bandwidth=axi_data_width, latency=1)

        # Core Complex
        for core_id in range(0, nb_cores_per_tile):
            if fast_model:
                self.int_cores.append(iss.SnitchFast(self, f'pe{core_id}', isa="rv32imaf",
                    core_id=group_id*nb_sub_groups_per_group*nb_tiles_per_sub_group*nb_cores_per_tile+sub_group_id*nb_tiles_per_sub_group*nb_cores_per_tile+tile_id*nb_cores_per_tile+core_id,
                    htif=False, pulp_v2=True
                ))
            else:
                self.int_cores.append(iss.Snitch(self, f'pe{core_id}', isa="rv32imaf", htif=False, \
                    core_id=group_id*nb_sub_groups_per_group*nb_tiles_per_sub_group*nb_cores_per_tile+sub_group_id*nb_tiles_per_sub_group*nb_cores_per_tile+tile_id*nb_cores_per_tile+core_id,
                    pulp_v2=True))
                self.fp_cores.append(iss.Snitch_fp_ss(self, f'fp_ss{core_id}', isa="rv32imaf", htif=False, \
                    core_id=group_id*nb_sub_groups_per_group*nb_tiles_per_sub_group*nb_cores_per_tile+sub_group_id*nb_tiles_per_sub_group*nb_cores_per_tile+tile_id*nb_cores_per_tile+core_id,
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

        for i in range(0, nb_remote_sub_group_ports):
            self.bind(self, f'sub_grp_remt{i}_slave_in', l1, f'remote_sub_group_in{i}')
            self.bind(l1, f'remote_sub_group_out{i}', self, f'sub_grp_remt{i}_master_out')

        for i in range(0, nb_remote_group_ports):
            self.bind(self, f'grp_remt{i}_slave_in', l1, f'remote_group_in{i}')
            self.bind(l1, f'remote_group_out{i}', self, f'grp_remt{i}_master_out')

        # ICO -> AXI -> L2 Memory
        for i in range(0, nb_cores_per_tile):
            # Add default mapping for the others
            ico_list[i].add_mapping('axi')
            self.bind(ico_list[i], 'axi', axi_ico, 'input')

        ###########################################################
        #                                       |--> ROM          #
        # Core -->|--icache--> |-->AXI router-->|--> CSR          #
        #                                       |--> L2 Memory    #
        #                                       |--> Dummy Memory #
        ###########################################################

        # Icache -> AXI
        self.bind(icache, 'refill', axi_ico, 'input')

        # AXI <-> ROM ports
        axi_ico.add_mapping('rom', base=0xa0000000, remove_offset=0xa0000000, size=0x1000)
        self.bind(axi_ico, 'rom', self, 'rom')

        # AXI <-> L2 Memory ports
        axi_ico.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x1000000)
        self.bind(axi_ico, 'mem', self, 'L2_data')

        # AXI -> CSR ports
        axi_ico.add_mapping('csr', base=0x40000000, remove_offset=0x40000000, size=0x10000)
        self.bind(axi_ico, 'csr', self, 'csr')

        # AXI -> UART ports
        axi_ico.add_mapping('uart', base=0xc0000000, remove_offset=0xc0000000, size=0x100)
        self.bind(axi_ico, 'uart', self, 'uart')

        # AXI -> Dummy Memory ports
        axi_ico.add_mapping('dummy')
        self.bind(axi_ico, 'dummy', self, 'dummy_mem')

        # Sync barrier
        for core_id in range(0, nb_cores_per_tile):
            self.bind(self, f'barrier_ack_{core_id}', self.int_cores[core_id], 'barrier_ack')

        # Core Interconnections
        for core_id in range(0, nb_cores_per_tile):
            # Icache
            self.bind(self.int_cores[core_id], 'flush_cache_req', icache, 'flush')
            self.bind(icache, 'flush_ack', self.int_cores[core_id], 'flush_cache_ack')

            # Snitch integer cores
            self.bind(self.int_cores[core_id], 'data', addr_scrambler_list[core_id], 'input')
            self.bind(self.int_cores[core_id], 'fetch', icache, 'input_%d' % core_id)
            self.bind(self, 'loader_start', self.int_cores[core_id], 'fetchen')
            self.bind(self, 'loader_entry', self.int_cores[core_id], 'bootaddr')

            if not fast_model:
                # Snitch fp subsystems
                # Pay attention to interactions and bandwidth between subsystem and tohost.
                self.bind(self.fp_cores[core_id], 'data', addr_scrambler_list[core_id], 'input')
                # FP subsystem doesn't fetch instructions from core->ico->memory, but from integer cores acc_req.
                self.bind(self, 'loader_start', self.fp_cores[core_id], 'fetchen')
                self.bind(self, 'loader_entry', self.fp_cores[core_id], 'bootaddr')

            # Scrambler
            self.bind(addr_scrambler_list[core_id], 'output', ico_list[core_id], 'input')

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
