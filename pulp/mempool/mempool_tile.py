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
from pulp.mempool.mempool_tile_submodule.dma_interleaver import DmaInterleaver
from pulp.idma.snitch_dma import SnitchDma
from interco.bus_watchpoint import Bus_watchpoint
from interco.sequencer import Sequencer
from pulp.spatz.cluster_registers import Cluster_registers
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
import math

GAPY_TARGET = True

class Tile(st.Component):

    def __init__(self, parent, name, parser, tile_id: int=0, group_id: int=0, nb_cores_per_tile: int=4, nb_groups: int=4, total_cores: int= 256, bank_factor: int=4):
        super().__init__(parent, name)

        [args, __] = parser.parse_known_args()

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        ################################################################
        ##########               Design Variables             ##########
        ################################################################
        # Hardware parameters 
        nb_remote_ports = nb_groups
        nb_tiles_per_group = int((total_cores/nb_groups)/nb_cores_per_tile)
        global_tile_id = tile_id + group_id * nb_tiles_per_group
        Xfrep = 1
        stack_size_per_tile = 0x1000
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

        # Stack Memory
        stack_mem = Memory(self, 'stack_mem', size=0x1000, width_log2=int(math.log(4, 2.0)), atomics=True, core='snitch', mem='tcdm')
        
        # Snitch TCDM (L1 subsystem)
        l1 = l1_subsystem.L1_subsystem(self, 'l1', \
                                        tile_id=tile_id, group_id=group_id, \
                                        nb_tiles_per_group=nb_tiles_per_group, nb_groups=nb_groups, \
                                        nb_pe=nb_cores_per_tile, nb_remote_masters=nb_remote_ports, \
                                        size=mem_size, bandwidth=4,nb_banks_per_tile=nb_cores_per_tile*bank_factor)
        # Shared icache
        icache = Hierarchical_cache(self, 'shared_icache', nb_cores=nb_cores_per_tile, has_cc=0, l1_line_size_bits=7)

        # Route
        ico_list=[]
        for i in range(0, nb_cores_per_tile):
            ico_list.append(router.Router(self, 'ico%d' % i, bandwidth=4, latency=1))
        stack_ico = router.Router(self, 'stack_ico', bandwidth=4, latency=1)
        axi_ico = router.Router(self, 'axi_ico', bandwidth=4, latency=1)

        # Core Complex
        for core_id in range(0, nb_cores_per_tile):
            self.int_cores.append(iss.Snitch(self, f'pe{core_id}', isa="rv32imaf", core_id=core_id))
            self.fp_cores.append(iss.Snitch_fp_ss(self, f'fp_ss{core_id}', isa="rv32imaf", core_id=core_id))
            if Xfrep:
                fpu_sequencers.append(Sequencer(self, f'fpu_sequencer{core_id}', latency=0))

        ################################################################
        ##########               Design Bindings              ##########
        ################################################################

        # Stack Memory (non-interleaved)
        for i in range(0, nb_cores_per_tile):
            ico_list[i].add_mapping('stack_mem', base=0x00000000 + global_tile_id * mem_size, remove_offset=0x00000000 + global_tile_id * mem_size, size=stack_size_per_tile)
            self.bind(ico_list[i], 'stack_mem', stack_ico, 'input')
        stack_ico.add_mapping('stack_mem', base=0x00000000 + global_tile_id * mem_size, remove_offset=0x00000000 + global_tile_id * mem_size, size=stack_size_per_tile)
        self.bind(stack_ico, 'stack_mem', stack_mem, 'input')

        # L1 TCDM
        for i in range(0, nb_cores_per_tile):
            ico_list[i].add_mapping('l1', base=0x00000000 + stack_size_per_tile * nb_tiles_per_group * nb_groups, remove_offset=0x00000000 + stack_size_per_tile * nb_tiles_per_group * nb_groups, size=total_cores * bank_factor * 1024)
            self.bind(ico_list[i], 'l1', l1, f'pe_in{i}')

        # L2 Memory
        for i in range(0, nb_cores_per_tile):
            ico_list[i].add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x1000000)
            self.bind(ico_list[i], 'mem', axi_ico, 'input')
        
        # remote TCDM ports
        self.bind(self, 'grp_local_slave_in', l1, 'remote_in0')
        self.bind(l1, 'remote_out0', self, 'grp_local_master_out')
        
        self.bind(self, 'grp_remt0_slave_in', l1, 'remote_in1')
        self.bind(l1, 'remote_out1', self, 'grp_remt0_master_out')
        
        self.bind(self, 'grp_remt1_slave_in', l1, 'remote_in2')
        self.bind(l1, 'remote_out2', self, 'grp_remt1_master_out')
        
        self.bind(self, 'grp_remt2_slave_in', l1, 'remote_in3')
        self.bind(l1, 'remote_out3', self, 'grp_remt2_master_out')

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

        # AXI -> Dummy Memory ports
        axi_ico.add_mapping('dummy')
        self.bind(axi_ico, 'dummy', self, 'dummy_mem')

        # RISCV bus watchpoint
        tohost_addr = 0
        fromhost_addr = 0
        entry = 0
        if binary is not None:
            with open(binary, 'rb') as file:
                elffile = ELFFile(file)
                entry = elffile['e_entry']
                for section in elffile.iter_sections():
                    if isinstance(section, SymbolTableSection):
                        for symbol in section.iter_symbols():
                            if symbol.name == 'tohost':
                                tohost_addr = symbol.entry['st_value']
                            if symbol.name == 'fromhost':
                                fromhost_addr = symbol.entry['st_value']

        # Core Interconnections
        for core_id in range(0, nb_cores_per_tile):
            bus_watchpoints.append(Bus_watchpoint(self, f'tohost{core_id}', tohost_addr, fromhost_addr, word_size=32))

        # Sync barrier
        for core_id in range(0, nb_cores_per_tile):
            self.bind(self.int_cores[core_id], 'barrier_req', self, f'barrier_req_{core_id}')
            self.bind(self, f'barrier_ack_{core_id}', self.int_cores[core_id], 'barrier_ack')

        for core_id in range(0, nb_cores_per_tile):
            #                                          |--> stack_ico --> stack_mem
            # Core --> Buswatchpoint --> ico router -->|--> L1 submodule
            #                                          |--> axi_ico --> L2 memory
            self.bind(bus_watchpoints[core_id], 'output', ico_list[core_id], 'input')

            # Icache
            self.bind(self.int_cores[core_id], 'flush_cache_req', icache, 'flush')
            self.bind(icache, 'flush_ack', self.int_cores[core_id], 'flush_cache_ack')
            
            # Snitch integer cores
            self.bind(self.int_cores[core_id], 'data', bus_watchpoints[core_id], 'input')
            self.bind(self.int_cores[core_id], 'data', axi_ico, 'input')
            self.bind(self.int_cores[core_id], 'fetch', icache, 'input_%d' % core_id)
            self.bind(self, 'loader_start', self.int_cores[core_id], 'fetchen')
            self.bind(self, 'loader_entry', self.int_cores[core_id], 'bootaddr')
            
            # Snitch fp subsystems
            # Pay attention to interactions and bandwidth between subsystem and tohost.
            self.bind(self.fp_cores[core_id], 'data', bus_watchpoints[core_id], 'input')
            # FP subsystem doesn't fetch instructions from core->ico->memory, but from integer cores acc_req.
            self.bind(self, 'loader_start', self.fp_cores[core_id], 'fetchen')
            self.bind(self, 'loader_entry', self.fp_cores[core_id], 'bootaddr')
            
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
