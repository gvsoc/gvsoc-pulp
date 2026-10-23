#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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

import gvsoc.runner
import gvsoc.systree

import interco.router as router
from interco.interleaver import Interleaver
import memory.memory as memory
from pulp.chips.mc_pool.mempool_dma.flex_mempool_dma import FlexMemPoolDma
from elftools.elf.elffile import *
from pulp.mempool.mempool_cluster import Cluster
from pulp.mempool.redmule_configurations import RedmuleParam
from pulp.mempool.l2_subsystem import L2_subsystem
from pulp.chips.mc_pool.cluster_registers import ClusterRegisters
from pulp.chips.mc_pool.util_dumpper import UtilDumpper

import gvsoc.runner
import math
import utils.loader.loader


GAPY_TARGET = True

#Function to get EoC entry
def find_binary_entry(elf_filename):
    # Open the ELF file in binary mode
    with open(elf_filename, 'rb') as f:
        elffile = ELFFile(f)

        # Find the symbol table section in the ELF file
        for section in elffile.iter_sections():
            if isinstance(section, SymbolTableSection):
                # Iterate over symbols in the symbol table
                for symbol in section.iter_symbols():
                    # Check if this symbol's name matches "tohost"
                    if symbol.name == '_start':
                        # Return the symbol's address
                        return symbol['st_value']

    # If the symbol wasn't found, return None
    return None



class Area:

    def __init__(self, base, size):
        self.base = base
        self.size = size



class ClusterArch:
    def __init__(self,  nb_core_per_cluster,
                        base,
                        tcdm_size,
                        cluster_id,
                        stack_base,         
                        stack_size,
                        reg_base,
                        reg_size,
                        num_cluster_x,
                        num_cluster_y,
                        insn_base,
                        insn_size,
                        terapool,
                        tensorpool,
                        nb_redmule_tiles,
                        redmule_height,
                        redmule_width,
                        redmule_regs,
                        nb_cores_per_tile,
                        nb_sub_groups_per_group,
                        nb_groups,
                        bank_factor,
                        axi_data_width,
                        dma_tcdm_outstanding,
                        dma_burst_queue_size,
                        nb_axi_masters_per_group,
                        nb_l2_banks,
                        sync_base,
                        sync_itlv,
                        sync_special_mem,
                        soc_register_base,
                        redmule_queue_depth=0,
                        redmule_stream_loads=False,
                        redmule_tcdm_bank_number=None,
                        bank_size=1024,
                        auto_fetch=False):

        self.base                   = base
        self.tcdm_size              = tcdm_size
        self.cluster_id             = cluster_id
        self.auto_fetch             = auto_fetch
        self.stack_area             = Area(stack_base, stack_size)
        self.reg_area               = Area(reg_base, reg_size)
        self.insn_area              = Area(insn_base, insn_size)

        # Cluster configuration
        self.async_l1_interco           = False
        self.terapool                   = terapool
        self.tensorpool                 = tensorpool
        self.nb_redmule_tiles           = nb_redmule_tiles
        self.redmule_height             = redmule_height
        self.redmule_width              = redmule_width
        self.redmule_regs               = redmule_regs
        self.redmule_queue_depth        = redmule_queue_depth
        self.redmule_stream_loads       = redmule_stream_loads
        self.redmule_tcdm_bank_number   = redmule_tcdm_bank_number
        self.nb_cores_per_tile          = nb_cores_per_tile
        self.nb_sub_groups_per_group    = nb_sub_groups_per_group
        self.nb_groups                  = nb_groups
        self.total_cores                = nb_core_per_cluster
        self.bank_factor                = bank_factor
        self.bank_size                  = bank_size
        self.axi_data_width             = axi_data_width
        self.dma_tcdm_outstanding       = dma_tcdm_outstanding
        self.dma_burst_queue_size       = dma_burst_queue_size
        self.nb_axi_masters_per_group   = nb_axi_masters_per_group
        self.nb_l2_banks                = nb_l2_banks

        #Global Information
        self.num_cluster_x          = num_cluster_x
        self.num_cluster_y          = num_cluster_y

        # Sync bus parameters
        self.sync_base              = sync_base
        self.sync_itlv              = sync_itlv
        self.sync_special_mem       = sync_special_mem
        self.sync_area              = Area(sync_base, sync_itlv + sync_special_mem)
        self.soc_register_base      = soc_register_base

class ClusterUnit(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, parser, entry=0, auto_fetch=True):
        super().__init__(parent, name)

        nb_axi_masters = arch.nb_axi_masters_per_group * arch.nb_groups

        # Tensor engines (RedMulE): build config only when engines are present
        redmule_config = RedmuleParam(
            tcdm_bank_number=arch.redmule_tcdm_bank_number,
            redmule_height=arch.redmule_height,
            redmule_width=arch.redmule_width,
            redmule_regs=arch.redmule_regs,
            queue_depth=arch.redmule_queue_depth,
            stream_loads=arch.redmule_stream_loads,
        ) if arch.nb_redmule_tiles > 0 else None

        # The MemPool cluster has one DMA TCDM port per sub-group of each group (or one per group
        # without sub-groups)
        nb_dmas_per_group = arch.nb_sub_groups_per_group

        #Mempool cluster
        mempool_cluster=Cluster( self, 'mempool_cluster',
                                 async_l1_interco=arch.async_l1_interco,
                                 redmule_config=redmule_config,
                                 tensorpool=arch.tensorpool,
                                 nb_redmule_tiles=arch.nb_redmule_tiles,
                                 terapool=arch.terapool,
                                 parser=parser,
                                 nb_cores_per_tile=arch.nb_cores_per_tile,
                                 nb_sub_groups_per_group=arch.nb_sub_groups_per_group, 
                                 nb_groups=arch.nb_groups, 
                                 total_cores=arch.total_cores, 
                                 bank_factor=arch.bank_factor,
                                 bank_size=arch.bank_size,
                                 axi_data_width=arch.axi_data_width,
                                 nb_axi_masters_per_group=arch.nb_axi_masters_per_group,
                                 nb_dmas_per_group=nb_dmas_per_group)

        # Boot Rom
        rom = memory.Memory(self, 'rom', size=0x1000, width_log2=(arch.axi_data_width - 1).bit_length(), stim_file=self.get_file_path('pulp/chips/spatz/rom.bin'))

        #stack memory
        stack_mem = memory.Memory(self, 'stack_mem', size=arch.stack_area.size)

        # Cluster CSRs (extended with inter-cluster barrier config)
        cluster_regs = ClusterRegisters(self, 'ctrl_registers',
            wakeup_latency=18 if arch.nb_sub_groups_per_group > 1 else 15,
            cluster_id=arch.cluster_id,
            num_cluster_x=arch.num_cluster_x,
            num_cluster_y=arch.num_cluster_y,
            global_barrier_addr=arch.sync_area.base+arch.sync_itlv,
            soc_register_base=arch.soc_register_base)

        # Data Dumpper
        data_dumpper = UtilDumpper(self, 'data_dumpper', arch.cluster_id)
        data_dumpper_ctrl_base = arch.reg_area.base + arch.reg_area.size
        data_dumpper_ctrl_size = 64
        data_dumpper_input_base = arch.base + arch.tcdm_size
        data_dumpper_input_size = arch.tcdm_size

        # DMA
        dma = FlexMemPoolDma(self, 'dma', loc_base=arch.base, loc_size=arch.tcdm_size + data_dumpper_input_size, tcdm_width=arch.axi_data_width,
                             tcdm_outstanding=arch.dma_tcdm_outstanding,
                             burst_queue_size=arch.dma_burst_queue_size)

        # Binary Loader
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry=arch.insn_area.base)
        self.loader = loader

        # Originally part of the L3 instruction memory logic in soft_hier/cluster_unit.py
        # L2 Memory
        # Efficient bandwidth of each port is only 1/4 of axi_data_width in current design
        l2_mem = L2_subsystem(self, 'l2_mem', nb_banks=arch.nb_l2_banks,
                              bank_width=arch.axi_data_width, size=arch.insn_area.size,
                              nb_masters=nb_axi_masters, port_bandwidth=arch.axi_data_width//4)
        
        #Dummy Memory
        dummy_mem = memory.Memory(self, 'dummy_mem', atomics=True, size=arch.tcdm_size)

        # AXI Interconnect
        axi_ico = []
        for i in range(0, nb_axi_masters):
            axi_ico.append(router.Router(self, f'axi_ico_{i}', latency=0))
            axi_ico[i].add_mapping('l2', base=arch.insn_area.base, remove_offset=arch.insn_area.base, size=arch.insn_area.size)
            axi_ico[i].add_mapping('soc')

        narrow_axi = router.Router(self, 'narrow_axi', bandwidth=8)
        narrow_axi.add_mapping('dummy', base=arch.base, remove_offset=arch.base, size=arch.tcdm_size)
        narrow_axi.add_mapping('bootrom', base=0xa0000000, remove_offset=0xa0000000, size=0x10000, latency=1)
        narrow_axi.add_mapping('csr_mempool_regs', base=0x40000000, remove_offset=0x40000000, size=0x10000, latency=1)
        narrow_axi.add_mapping('dma_ctrl', base=0x40010000, remove_offset=0x40010000, size=0x10000, latency=1)
        narrow_axi.add_mapping('csr_barrier_regs', base=arch.reg_area.base, remove_offset=arch.reg_area.base, size=arch.reg_area.size, latency=1)
        narrow_axi.add_mapping('data_dumpper_ctrl', base=data_dumpper_ctrl_base, remove_offset=data_dumpper_ctrl_base, size=data_dumpper_ctrl_size, latency=1)

        # Binary Loader Router
        instr_router = router.Router(self, 'instr_router', bandwidth=8*arch.total_cores)

        ###################
        ## INTERCONNECTS ##
        ###################

        # Group axi port -> axi interconnect
        for i in range(0, nb_axi_masters):
            self.bind(mempool_cluster, 'axi_%d' % i, axi_ico[i], 'input')

        # SoC interconnect
        for i in range(0, nb_axi_masters):
            self.bind(axi_ico[i], 'soc', narrow_axi, 'input')

        # FlexCluster virtual router interconnect
        self.o_NARROW_INPUT(narrow_axi.i_INPUT())
        # Forward SoC control register window (e.g. EOC) to the narrow SoC path.
        narrow_axi.o_MAP(self.i_NARROW_SOC())

        # Bootrom
        self.bind(narrow_axi, 'bootrom', rom, 'input')

        # Dummy memory
        self.bind(narrow_axi, 'dummy', dummy_mem, 'input')

        #binding to stack memory
        narrow_axi.o_MAP(stack_mem.i_INPUT(), base=arch.stack_area.base, size=arch.stack_area.size, rm_base=True)

        # L2
        for i in range(0, nb_axi_masters):
            self.bind(axi_ico[i], 'l2', l2_mem, 'input_%d' % i)

        # CSR Mempool regs (0x40000000) 
        self.bind(narrow_axi, 'csr_mempool_regs', cluster_regs, 'input')

        # Barrier config registers (0x20000000) mapping
        self.bind(narrow_axi, 'csr_barrier_regs', cluster_regs, 'barrier_reg_input')

        # DMA Ctrl
        self.bind(narrow_axi, 'dma_ctrl', dma, 'input')

        # Data Dumpper Ctrl
        self.bind(narrow_axi, 'data_dumpper_ctrl', data_dumpper, 'ctrl')

        # Binary loader
        loader.o_OUT(instr_router.i_INPUT())
        self.bind(loader, 'entry', mempool_cluster, 'loader_entry')
        loader.o_START(cluster_regs.i_INST_PREHEAT_DONE())
        self.bind(cluster_regs, 'fetch_start', mempool_cluster, 'loader_start')   
        self.o_HBM_PRELOAD_DONE(cluster_regs.i_HBM_PRELOAD_DONE())

        #loader router
        instr_router.o_MAP(narrow_axi.i_INPUT())
        instr_router.add_mapping('mem', base=arch.insn_area.base, remove_offset=arch.insn_area.base, size=arch.insn_area.size)
        self.bind(instr_router, 'mem', l2_mem, 'input_loader')

        #L2 ro-cache configuration
        self.bind(cluster_regs, 'rocache_cfg', mempool_cluster, 'rocache_cfg')

        # Mempool EOC MMIO notification path:
        # cluster_regs -> narrow_axi -> narrow_soc -> virtual_interco.
        self.bind(cluster_regs, 'cluster_eoc', narrow_axi, 'input')

        #DMA data
        # The TCDM accesses of the DMA and of remote clusters are split over the DMA ports of the
        # MemPool cluster: L1 is interleaved over the tiles every 32-bit x banks-per-tile bytes,
        # so each DMA port (sub-group of a group) owns consecutive blocks of its banks.
        nb_dma_ports = arch.nb_groups * nb_dmas_per_group
        banks_per_dma_port = arch.total_cores * arch.bank_factor // nb_dma_ports
        dma_tcdm_split = Interleaver(self, 'dma_tcdm_split', nb_slaves=nb_dma_ports, nb_masters=1,
            interleaving_bits=int(math.log2(4 * banks_per_dma_port)), offset_translation=False)
        for i in range(arch.nb_groups):
            for j in range(nb_dmas_per_group):
                self.bind(dma_tcdm_split, f'out_{i * nb_dmas_per_group + j}', mempool_cluster, f'dma_tcdm_{i}_{j}')

        #To emulate distributed backends in groups
        data_dumpper_arbiter = router.Router(self, 'data_dumpper_arbiter')
        data_dumpper_arbiter.add_mapping('dma_tcdm')
        data_dumpper_arbiter.add_mapping('data_dumpper_input', base=data_dumpper_input_base, remove_offset=data_dumpper_input_base, size=data_dumpper_input_size)
        self.bind(data_dumpper_arbiter, 'dma_tcdm', dma_tcdm_split, 'input')
        self.bind(data_dumpper_arbiter, 'data_dumpper_input', data_dumpper, 'input')
        self.bind(dma, 'tcdm_read', data_dumpper_arbiter, 'input')
        self.bind(dma, 'tcdm_write', data_dumpper_arbiter, 'input')

        # Wire router for incoming remote cluster TCDM accesses via data NoC
        wide_axi_goto_tcdm = router.Router(self, 'wide_axi_goto_tcdm')
        self.o_WIDE_INPUT(wide_axi_goto_tcdm.i_INPUT())
        wide_axi_goto_tcdm.add_mapping('dma_tcdm')
        self.bind(wide_axi_goto_tcdm, 'dma_tcdm', dma_tcdm_split, 'input')

        # Wide SoC path for remote cluster TCDM and HBM accesses via data NoC
        wide_axi_from_idma = router.Router(self, 'wide_axi_from_idma')
        wide_axi_from_idma.o_MAP(self.i_WIDE_SOC())
        self.bind(dma, 'axi_read', wide_axi_from_idma, 'input')
        self.bind(dma, 'axi_write', wide_axi_from_idma, 'input')

        ################
        ## SYNC BUS   ##
        ################

        #Cluster Registers for synchronization barrier
        for i in range(0, arch.total_cores):
            self.bind(cluster_regs, f'barrier_ack', mempool_cluster, f'barrier_ack_{i}')

        # Sync memory for barrier counters (atomics-capable)
        sync_mem = memory.Memory(self, 'sync_mem', size=arch.sync_itlv, atomics=True, width_log2=2)

        # Outgoing: narrow_axi 'sync' -> sync_router_master -> SYNC_OUTPUT -> sync_bus
        sync_router_master = router.Router(self, 'sync_router_master', bandwidth=4)
        sync_router_master.o_MAP(self.i_SYNC_OUTPUT())
        narrow_axi.o_MAP(sync_router_master.i_INPUT(), base=arch.sync_area.base, size=arch.sync_area.size*arch.num_cluster_x*arch.num_cluster_y, rm_base=False)

        # cluster_regs global_barrier_master -> sync_router_master (outgoing wakeup)
        cluster_regs.o_GLOBAL_BARRIER_MASTER(sync_router_master.i_INPUT())

        # Incoming: SYNC_INPUT -> sync_router_slave -> sync_mem / global_barrier_slave
        sync_router_slave = router.Router(self, 'sync_router_slave', bandwidth=4)
        self.o_SYNC_INPUT(sync_router_slave.i_INPUT())
        sync_router_slave.add_mapping('sync_mem')
        self.bind(sync_router_slave, f'sync_mem', sync_mem, f'input')

        # sync_router_slave -> cluster_regs global_barrier_slave (incoming wakeup)
        sync_router_slave.o_MAP(cluster_regs.i_GLOBAL_BARRIER_SLAVE(), base=arch.sync_itlv, size=arch.sync_special_mem, rm_base=True)

    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input', signature='io')

    def o_WIDE_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_input', itf, signature='io', composite_bind=True)

    def i_WIDE_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_soc', signature='io')

    def o_WIDE_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_soc', itf, signature='io')

    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature='io')

    def o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature='io', composite_bind=True)

    def i_NARROW_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_soc', signature='io')

    def o_NARROW_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_soc', itf, signature='io')

    def i_SYNC_OUTPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'sync_output', signature='io')

    def o_SYNC_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('sync_output', itf, signature='io')

    def i_SYNC_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'sync_input', signature='io')

    def o_SYNC_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('sync_input', itf, signature='io', composite_bind=True)

    def i_HBM_PRELOAD_DONE(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm_preload_done', signature='wire<bool>')

    def o_HBM_PRELOAD_DONE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm_preload_done', itf, signature='wire<bool>', composite_bind=True)