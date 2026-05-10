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
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from pulp.snitch.zero_mem import ZeroMem
from elftools.elf.elffile import *
from pulp.idma.snitch_dma import SnitchDma
from pulp.cluster.l1_interleaver import L1_interleaver
import gvsoc.runner
import math
from pulp.snitch.sequencer import Sequencer
import utils.loader.loader
from pulp.chips.softhier.cluster_csr import ClusterCSR


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


class ClusterArch:
    def __init__(
        self,  
        num_core,           cluster_id,
        spatz_num_lane,     spatz_lane_width,
        tcdm_bank_nb,       tcdm_bank_width,
        inst_base,          inst_size,
        tcdm_base,          tcdm_size,
        stack_base,         stack_size,
        zomem_base,         zomem_size,
        reg_base,           reg_size,
        idma_outstand_txn,  idma_outstand_burst,
        auto_fetch=False):

        self.num_core               = num_core
        self.cluster_id             = cluster_id
        self.spatz_num_lane         = spatz_num_lane
        self.spatz_lane_width       = spatz_lane_width
        self.tcdm_bank_nb           = tcdm_bank_nb
        self.tcdm_bank_width        = tcdm_bank_width
        self.inst_base              = inst_base
        self.inst_size              = inst_size
        self.tcdm_base              = tcdm_base
        self.tcdm_size              = tcdm_size
        self.stack_base             = stack_base
        self.stack_size             = stack_size
        self.zomem_base             = zomem_base
        self.zomem_size             = zomem_size
        self.reg_base               = reg_base
        self.reg_size               = reg_size
        self.idma_outstand_txn      = idma_outstand_txn
        self.idma_outstand_burst    = idma_outstand_burst
        self.auto_fetch             = auto_fetch


class ClusterTcdm(gvsoc.systree.Component):

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        banks = []
        nb_banks = arch.tcdm_bank_nb
        bank_size = (arch.tcdm_size / arch.tcdm_bank_nb) + arch.tcdm_bank_width
        nb_masters = arch.num_core * (1 + arch.spatz_num_lane) # core + spatz lanes
        for i in range(0, nb_banks):
            banks.append(memory.Memory(self, f'bank_{i}', size=bank_size, atomics=True, width_log2=int(math.log2(arch.tcdm_bank_width))))

        interleaver = L1_interleaver(self, 'interleaver', nb_slaves=nb_banks,
            nb_masters=nb_masters, interleaving_bits=int(math.log2(arch.tcdm_bank_width)))

        dma_interleaver = DmaInterleaver(self, 'dma_interleaver', 2,
            nb_banks, arch.tcdm_bank_width)

        for i in range(0, nb_banks):
            self.bind(interleaver, 'out_%d' % i, banks[i], 'input')
            self.bind(dma_interleaver, 'out_%d' % i, banks[i], 'input')

        for i in range(0, nb_masters):
            self.bind(self, f'in_{i}', interleaver, f'in_{i}')

        self.bind(self, f'dma_input', dma_interleaver, f'input')
        self.bind(self, f'bus_input', dma_interleaver, f'input')

    def i_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'in_{port}', signature='io')

    def i_DMA_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'dma_input', signature='io')

    def i_BUS_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'bus_input', signature='io')



class ClusterUnit(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary):
        super().__init__(parent, name)

        #
        # Components
        #

        # Boot Address
        boot_addr = 0x8000_0000
        if binary is not None:
            boot_addr = find_binary_entry(binary)

        # Loader
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        # Instruction memory
        instr_mem = memory.Memory(self, 'instr_mem', size=arch.inst_size, atomics=True, width_log2=-1)

        #Instruction router
        instr_router = router.Router(self, 'instr_router')

        # Cores
        cores = []
        for i in range(arch.num_core):
            cores.append(iss.SnitchFast(self, f'core_{i}', isa='rv32imfdva',
                        fetch_enable=arch.auto_fetch, boot_addr=boot_addr,
                        core_id=i, htif=False,
                        inc_spatz=True,
                        spatz_nb_lanes=arch.spatz_num_lane,
                        spatz_lane_width=arch.spatz_lane_width
                    ))

        # TCDM
        tcdm = ClusterTcdm(self, 'tcdm', arch)

        # Stack memory
        stack_mem = memory.Memory(self, 'stack_mem', size=arch.stack_size)

        # Core interco
        core_icos = []
        for i in range(arch.num_core):
            core_icos.append(router.Router(self, f'core_ico_{i}'))

        # Narrow AXI
        narrow_axi = router.Router(self, 'narrow_axi', bandwidth=8)

        # CSR
        csr = ClusterCSR(self, 'csr', nb_cores=arch.num_core, cluster_id=arch.cluster_id)

        # iDMA
        idma = SnitchDma(self, 'idma', loc_base=arch.tcdm_base, loc_size=arch.tcdm_size,
                tcdm_width=(arch.tcdm_bank_nb * arch.tcdm_bank_width), transfer_queue_size=arch.idma_outstand_txn, burst_queue_size=arch.idma_outstand_burst)


        #
        # Bindings
        #

        # Binary loader
        loader.o_OUT(instr_router.i_INPUT())
        for i in range(arch.num_core):
            loader.o_START(cores[i].i_FETCHEN())
        instr_router.o_MAP(instr_mem.i_INPUT(), base=arch.inst_base, size=arch.inst_size, rm_base=True)

        # Core
        for i in range(arch.num_core):
            cores[i].o_DATA(core_icos[i].i_INPUT())
            cores[i].o_FETCH(instr_router.i_INPUT())
            cores[i].o_BARRIER_REQ(csr.i_BARRIER_ACK(i))
            self.bind(csr, f'barrier_ack', cores[i], 'barrier_ack')
            for lane in range(arch.spatz_num_lane):
                vlsu_router = router.Router(self, f'core{i}_spatz_lane{lane}_router', bandwidth=arch.spatz_lane_width)
                vlsu_router.add_mapping("output")
                cores[i].o_VLSU(lane, vlsu_router.i_INPUT())
                self.bind(vlsu_router, 'output', tcdm, f'in_{arch.num_core + i * arch.spatz_num_lane + lane}')

        # Last Core with iDMA
        cores[arch.num_core-1].o_OFFLOAD(idma.i_OFFLOAD())
        idma.o_OFFLOAD_GRANT(cores[arch.num_core-1].i_OFFLOAD_GRANT())

        # Core interco
        for i in range(arch.num_core):
            core_icos[i].o_MAP(narrow_axi.i_INPUT())
            core_icos[i].o_MAP(instr_router.i_INPUT(),  base=arch.inst_base,    size=arch.inst_size,    rm_base=False)
            core_icos[i].o_MAP(stack_mem.i_INPUT(),     base=arch.stack_base,   size=arch.stack_size,   rm_base=True)
            core_icos[i].o_MAP(tcdm.i_INPUT(i),         base=arch.tcdm_base,    size=arch.tcdm_size,    rm_base=True)

        # Narrow AXI
        narrow_axi.o_MAP(self.i_NARROW_SOC())
        narrow_axi.o_MAP(csr.i_INPUT(),             base=arch.reg_base,     size=arch.reg_size,     rm_base=True)

        # iDMA
        idma.o_TCDM(tcdm.i_DMA_INPUT())
        idma.o_AXI(self.i_WIDE_SOC())

        # Narrow Input from SoC
        self.o_NARROW_INPUT(tcdm.i_INPUT(0))

        # Wide Input from SoC
        self.o_WIDE_INPUT(tcdm.i_BUS_INPUT())

    def i_NARROW_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_soc', signature='io')

    def o_NARROW_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_soc', itf, signature='io')

    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature='io')

    def o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature='io', composite_bind=True)

    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input', signature='io')

    def o_WIDE_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_input', itf, signature='io', composite_bind=True)

    def i_WIDE_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_soc', signature='io')

    def o_WIDE_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_soc', itf, signature='io')


