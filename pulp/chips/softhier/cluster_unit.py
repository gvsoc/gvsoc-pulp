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
import pulp.snitch.snitch_core as iss
import memory.memory as memory
import interco.router as router
import gvsoc.systree
from pulp.chips.softhier.cluster_registers import ClusterRegisters
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from pulp.chips.softhier.cluster_icache import ClusterICache
from pulp.snitch.zero_mem import ZeroMem
from elftools.elf.elffile import *
from pulp.idma.snitch_dma import SnitchDma
from pulp.cluster.l1_interleaver import L1_interleaver
import gvsoc.runner
import math
from pulp.snitch.sequencer import Sequencer
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
    def __init__(self,  nb_core_per_cluster, cluster_id, tcdm_size,
                        base,               remote_base, 
                        stack_base,         stack_size,
                        zomem_base,         zomem_size,
                        reg_base,           reg_size,
                        insn_base,          insn_size,
                        nb_tcdm_banks,      tcdm_bank_width,
                        idma_outstand_txn,  idma_outstand_burst,
                        num_cluster_x,      num_cluster_y,
                        spatz_core_list,    spatz_num_vlsu,
                        auto_fetch=False,   multi_idma_enable=0):

        self.nb_core                = nb_core_per_cluster
        self.base                   = base
        self.remote_base            = remote_base
        self.cluster_id             = cluster_id
        self.auto_fetch             = auto_fetch
        self.barrier_irq            = 19
        self.tcdm                   = ClusterArch.Tcdm(base, self.nb_core + len(spatz_core_list)*spatz_num_vlsu, tcdm_size, nb_tcdm_banks, tcdm_bank_width, stack_base, stack_size)
        self.stack_area             = Area(stack_base, stack_size)
        self.zomem_area             = Area(zomem_base, zomem_size)
        self.reg_area               = Area(reg_base, reg_size)
        self.insn_area              = Area(insn_base, insn_size)

        #Spatz
        self.spatz_core_list        = spatz_core_list
        self.spatz_num_vlsu         = spatz_num_vlsu

        #IDMA
        self.idma_outstand_txn      = idma_outstand_txn
        self.idma_outstand_burst    = idma_outstand_burst
        self.multi_idma_enable      = multi_idma_enable

        #Global Information
        self.num_cluster_x          = num_cluster_x
        self.num_cluster_y          = num_cluster_y

    class Tcdm:
        def __init__(self, base, nb_masters, tcdm_size, nb_tcdm_banks, tcdm_bank_width, stack_base, stack_size):
            self.area = Area( base, tcdm_size)
            self.nb_tcdm_banks = nb_tcdm_banks
            self.bank_width = tcdm_bank_width
            self.bank_size = (self.area.size // self.nb_tcdm_banks) + self.bank_width #prevent overflow due to RedMule access model
            self.nb_masters = nb_masters
            self.stack_base = stack_base
            self.stack_size = stack_size
            self.stack_bank_size = (stack_size // self.nb_tcdm_banks) + self.bank_width


class ClusterTcdm(gvsoc.systree.Component):

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        banks = []
        stack_banks = []
        bank_arbiters = []
        nb_banks = arch.nb_tcdm_banks
        for i in range(0, nb_banks):
            banks.append(memory.Memory(self, f'bank_{i}', size=arch.bank_size, atomics=True, width_log2=int(math.log2(arch.bank_width))))
            stack_banks.append(memory.Memory(self, f'stack_bank_{i}', size=arch.stack_bank_size, atomics=True, width_log2=int(math.log2(arch.bank_width))))
            bank_arbiters.append(router.Router(self, f'bank_arbiter_{i}', bandwidth=arch.bank_width))

        interleaver = L1_interleaver(self, 'interleaver', nb_slaves=nb_banks,
            nb_masters=arch.nb_masters, interleaving_bits=int(math.log2(arch.bank_width)))

        dma_interleaver = DmaInterleaver(self, 'dma_interleaver', arch.nb_masters,
            nb_banks, arch.bank_width)

        bus_interleaver = DmaInterleaver(self, 'bus_interleaver', arch.nb_masters,
            nb_banks, arch.bank_width)

        for i in range(0, nb_banks):
            self.bind(interleaver, 'out_%d' % i,        bank_arbiters[i], 'input')
            self.bind(dma_interleaver, 'out_%d' % i,    bank_arbiters[i], 'input')
            self.bind(bus_interleaver, 'out_%d' % i,    bank_arbiters[i], 'input')
            bank_arbiters[i].o_MAP(stack_banks[i].i_INPUT(), base=arch.stack_base // arch.nb_tcdm_banks, size=arch.stack_size, rm_base=True)
            bank_arbiters[i].o_MAP(banks[i].i_INPUT())


        for i in range(0, arch.nb_masters):
            self.bind(self, f'in_{i}', interleaver, f'in_{i}')
            self.bind(self, f'dma_input', dma_interleaver, f'input')
            self.bind(self, f'bus_input', bus_interleaver, f'input')

    def i_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'in_{port}', signature='io')

    def i_DMA_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'dma_input', signature='io')

    def i_BUS_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'bus_input', signature='io')



class ClusterUnit(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, entry=0, auto_fetch=True):
        super().__init__(parent, name)

        #
        # Components
        #

        #Boot Address
        boot_addr = 0x8000_0000
        if binary is not None:
            boot_addr = find_binary_entry(binary)

        #Instruction Cache
        icache_l1_nb_ways_bits = 4
        icache = ClusterICache(self, 'icache', nb_cores=arch.nb_core, has_cc=0, l1_nb_sets_bits=int(math.log2(arch.insn_area.size))-icache_l1_nb_ways_bits, l1_nb_ways_bits=icache_l1_nb_ways_bits)

        # Main routers
        wide_bus_to_tcdm = router.Router(self, 'wide_bus_to_tcdm')
        wide_axi = router.Router(self, 'wide_axi')
        narrow_axi = router.Router(self, 'narrow_axi', bandwidth=8)
        narrow_bus_to_tcdm = router.Router(self, 'narrow_bus_to_tcdm', bandwidth=8)

        # L1 Memory
        tcdm = ClusterTcdm(self, 'tcdm', arch.tcdm)

        # Cores
        cores = []
        cores.append(iss.SnitchFast(self, f'pe{core_id}', isa='rv32imfdva',
                fetch_enable=arch.auto_fetch, boot_addr=arch.boot_addr,
                ccore_id=core_id, htif=False,
                inc_spatz=(len(arch.spatz_core_list) > 0), spatz_nb_lanes=arch.spatz_num_vlsu,
                spatz_lane_width=arch.tcdm.bank_width
            ))

        cores_ico = []
        cores_ico.append(router.Router(self, f'pe{core_id}_ico', bandwidth=arch.tcdm.bank_width))

        # Cluster peripherals
        cluster_registers = ClusterRegisters(self, 'cluster_registers',
            num_cluster_x=arch.num_cluster_x, num_cluster_y=arch.num_cluster_y, nb_cores=arch.nb_core,
            boot_addr=boot_addr, cluster_id=arch.cluster_id, global_barrier_addr=arch.sync_area.base+arch.tcdm.sync_itlv)

        # Cluster DMA
        if arch.multi_idma_enable:
            idma_list = []
            for x in range(arch.nb_core):
                idma_list.append(SnitchDma(self, f'idma_{x}', loc_base=arch.tcdm.area.base, loc_size=arch.tcdm.area.size + data_dumpper_input_size,
                tcdm_width=(arch.tcdm.nb_tcdm_banks * arch.tcdm.bank_width), transfer_queue_size=arch.idma_outstand_txn, burst_queue_size=arch.idma_outstand_burst))
                pass
        else:
            idma = SnitchDma(self, 'idma', loc_base=arch.tcdm.area.base, loc_size=arch.tcdm.area.size + data_dumpper_input_size,
                tcdm_width=(arch.tcdm.nb_tcdm_banks * arch.tcdm.bank_width), transfer_queue_size=arch.idma_outstand_txn, burst_queue_size=arch.idma_outstand_burst)
            pass

        #zero memory
        zero_mem = ZeroMem(self, 'zero_mem', size=arch.zomem_area.size)


        #
        # Bindings
        #

        # Narrow router for cores data accesses
        self.o_NARROW_INPUT(narrow_bus_to_tcdm.i_INPUT())
        narrow_bus_to_tcdm.o_MAP(tcdm.i_INPUT(0))
        narrow_axi.o_MAP(self.i_NARROW_SOC())
        narrow_axi.o_MAP(cluster_registers.i_INPUT(), base=arch.reg_area.base, size=arch.reg_area.size, rm_base=True)


        # Wire router for DMA and Zomem
        self.o_WIDE_INPUT(wide_bus_to_tcdm.i_INPUT())
        wide_bus_to_tcdm.o_MAP(tcdm.i_BUS_INPUT())
        wide_axi.o_MAP(self.i_WIDE_SOC())
        wide_axi.o_MAP(zero_mem.i_INPUT(), base=arch.zomem_area.base, size=arch.zomem_area.size, rm_base=True)


        # iDMA connection
        if arch.multi_idma_enable:
            for x in range(arch.nb_core):
                cores[x].o_OFFLOAD(idma_list[x].i_OFFLOAD())
                idma_list[x].o_OFFLOAD_GRANT(cores[x].i_OFFLOAD_GRANT())
                pass
        else:
            cores[arch.nb_core-1].o_OFFLOAD(idma.i_OFFLOAD())
            idma.o_OFFLOAD_GRANT(cores[arch.nb_core-1].i_OFFLOAD_GRANT())
            pass

        # Cores  
        for core_id in range(0, arch.nb_core):
            cores[core_id].o_DATA(cores_ico[core_id].i_INPUT())
            cores_ico[core_id].o_MAP(tcdm.i_INPUT(core_id), base=arch.tcdm.area.base,
                size=arch.tcdm.area.size, rm_base=True)
            cores_ico[core_id].o_MAP(narrow_axi.i_INPUT())
            cores[core_id].o_FETCH(icache.i_INPUT(core_id))
            if core_id in arch.spatz_core_list:
                spatz_index = arch.spatz_core_list.index(core_id)
                for vlsu_port in range(arch.spatz_num_vlsu):
                    vlsu_router = router.Router(self, f'spatz_{core_id}_vlsu_{vlsu_port}_router', bandwidth=arch.tcdm.bank_width)
                    vlsu_router.add_mapping("output")
                    cores[core_id].o_VLSU(vlsu_port, vlsu_router.i_INPUT())
                    self.bind(vlsu_router, 'output', tcdm, f'in_{arch.nb_core + spatz_index*arch.spatz_num_vlsu + vlsu_port}')

        # Icache refill
        icache.o_REFILL(wide_axi.i_INPUT())

        # Bind control registers
        self.o_HBM_PRELOAD_DONE(cluster_registers.i_HBM_PRELOAD_DONE())
        for core_id in range(0, arch.nb_core):
            cores[core_id].o_BARRIER_REQ(cluster_registers.i_BARRIER_ACK(core_id))
            self.bind(cluster_registers, f'barrier_ack', cores[core_id], 'barrier_ack')
            cluster_registers.o_EXTERNAL_IRQ(core_id, cores[core_id].i_IRQ(arch.barrier_irq))
            cluster_registers.o_FETCH_START( cores[core_id].i_FETCHEN() )


        # Cluster DMA
        if arch.multi_idma_enable:
            for x in range(arch.nb_core):
                idma_list[x].o_TCDM(wide_bus_to_tcdm.i_INPUT())
                idma_list[x].o_AXI(wide_axi.i_INPUT())
                pass
        else:
            idma.o_TCDM(wide_bus_to_tcdm.i_INPUT())
            idma.o_AXI(wide_axi.i_INPUT())
            pass

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

    def i_HBM_PRELOAD_DONE(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm_preload_done', signature='wire<bool>')

    def o_HBM_PRELOAD_DONE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm_preload_done', itf, signature='wire<bool>', composite_bind=True)
