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


"""SoftHier cluster, io_v2 generation.

Same architecture as the v1 SoftHier cluster (pulp.chips.softhier), built
from io_v2 models:

- Spatz cores (iss_v2) with the Snitch hardware barrier, their vector
  memory ports wired straight to the TCDM crossbar.
- TCDM: SpatzTcdmInterco word-interleaved crossbar over memory_v3 banks. The
  narrow masters are the scalar ports of the cores, the vector ports and the
  narrow NoC input; the wide masters are the iDMA (through the cluster wide
  crossbar) and the wide NoC input.
- iDMA v2 (Snitch offload front-end) on the wide crossbar: accesses to the
  local TCDM loop back to it, the rest leaves on the wide SoC port.
- memory_v3 instruction and stack memories, and the cluster registers with
  the hardware barrier.
"""

import math

import gvsoc.systree
from gvsoc.signature import IoV2Beat, IoV2SingleReq
from elftools.elf.elffile import ELFFile, SymbolTableSection
import memory.memory_v3 as memory_v3
from memory.memory_v3 import MemoryV3Config
import interco.router_v2 as router_v2
from interco.router_v2 import RouterConfig, RouterMapping, KIND_UNTIMED, KIND_BEAT
from pulp.snitch.snitch_cluster.spatz.spatz_tcdm_interco import (
    SpatzTcdmInterco, SpatzTcdmIntercoConfig)
from pulp.cpu.iss.spatz import Spatz
from pulp.cpu.iss.spatz_config import SpatzConfig
from ips.pulp.idma_v2.snitch_dma import SnitchDmaV2
import utils.loader.loader_v2 as loader_v2
from pulp.chips.softhier_v2.common.cluster_csr import ClusterCSR, ClusterCsrConfig


GAPY_TARGET = True

# Width in bytes of the narrow (core, peripheral) plane.
NARROW_WIDTH = 8


def find_binary_entry(elf_filename):
    """Address of the _start symbol of an ELF file, None if not found."""
    with open(elf_filename, 'rb') as f:
        elffile = ELFFile(f)
        for section in elffile.iter_sections():
            if isinstance(section, SymbolTableSection):
                for symbol in section.iter_symbols():
                    if symbol.name == '_start':
                        return symbol['st_value']
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
        wide_width,
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
        # Width in bytes of the wide plane (iDMA, wide NoC)
        self.wide_width             = wide_width
        self.auto_fetch             = auto_fetch

    @property
    def nb_tcdm_masters(self):
        # Scalar port + vector ports of each core, and the narrow NoC input
        return self.num_core * (1 + self.spatz_num_lane) + 1


class ClusterTcdm(gvsoc.systree.Component):
    """TCDM: word-interleaved banks behind the SpatzTcdmInterco crossbar.

    Narrow input i is a single-request TCDM port, the two wide inputs (iDMA,
    NoC) claim every bank an access spans for one cycle, with priority over
    the narrow ones.
    """

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        nb_banks = arch.tcdm_bank_nb
        ico = SpatzTcdmInterco(self, 'ico', config=SpatzTcdmIntercoConfig(
            nb_masters=arch.nb_tcdm_masters,
            nb_slaves=nb_banks,
            interleaving_width=int(math.log2(arch.tcdm_bank_width)),
            nb_wide_masters=2))

        for i in range(0, nb_banks):
            bank = memory_v3.Memory(self, f'bank_{i}', config=MemoryV3Config(
                size=arch.tcdm_size // nb_banks, atomics=True))
            ico.o_OUTPUT(i, bank.i_INPUT())

        for i in range(0, arch.nb_tcdm_masters):
            self.bind(self, f'input_{i}', ico, f'input_{i}')
        for i in range(0, 2):
            self.bind(self, f'wide_input_{i}', ico, f'wide_input_{i}')

    def i_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'input_{port}', signature=IoV2SingleReq())

    def i_WIDE_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'wide_input_{port}', signature=IoV2SingleReq())


class ClusterUnit(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary):
        super().__init__(parent, name)

        self.wide_width = arch.wide_width

        # Boot address: the ELF entry, or the start of the instruction memory
        # when the binary is only known later (gvrun flow).
        boot_addr = arch.inst_base
        if binary is not None:
            boot_addr = find_binary_entry(binary)

        #
        # Components
        #

        # Loader, writing the binary into the instruction memory
        loader = loader_v2.ElfLoader(self, 'loader', binary=binary)
        self.loader = loader

        # Instruction memory, shared by the loader, the fetch and the data
        # ports of the cores
        instr_mem = memory_v3.Memory(self, 'instr_mem', config=MemoryV3Config(
            size=arch.inst_size, atomics=True))
        instr_router = router_v2.Router(self, 'instr_router', config=RouterConfig(
            kind=KIND_UNTIMED))

        # Stack memory
        stack_mem = memory_v3.Memory(self, 'stack_mem', config=MemoryV3Config(
            size=arch.stack_size, atomics=True))
        stack_router = router_v2.Router(self, 'stack_router', config=RouterConfig(
            kind=KIND_UNTIMED))

        # TCDM
        tcdm = ClusterTcdm(self, 'tcdm', arch)

        # Cores, with a per-core demux on the scalar data port
        cores = []
        core_demux = []
        for i in range(arch.num_core):
            cores.append(Spatz(self, f'core_{i}', config=SpatzConfig(
                isa='rv32imafdv', fetch_enable=arch.auto_fetch, boot_addr=boot_addr,
                hart_id=i, htif=False, vlsu_v2=True, barrier_csr=True,
                nb_lanes=arch.spatz_num_lane, lane_width=arch.spatz_lane_width)))
            core_demux.append(router_v2.Router(self, f'core_demux_{i}', config=RouterConfig(
                kind=KIND_UNTIMED)))

        # Narrow crossbar: cluster registers and SoC
        narrow_axi = router_v2.Router(self, 'narrow_axi', config=RouterConfig(
            kind=KIND_BEAT, width=NARROW_WIDTH))

        # Wide crossbar: iDMA accesses to the local TCDM and to the SoC
        wide_axi = router_v2.Router(self, 'wide_axi', config=RouterConfig(
            kind=KIND_BEAT, width=arch.wide_width))

        # Cluster registers and hardware barrier
        csr = ClusterCSR(self, 'csr', config=ClusterCsrConfig(nb_cores=arch.num_core,
            cluster_id=arch.cluster_id))

        # iDMA, offloaded from the last core
        idma = SnitchDmaV2(self, 'idma', transfer_queue_size=arch.idma_outstand_txn,
            burst_queue_size=arch.idma_outstand_burst, axi_width=arch.wide_width)

        #
        # Bindings
        #

        # Binary loader
        loader.o_OUT(instr_router.i_INPUT(0))
        for i in range(arch.num_core):
            loader.o_START(cores[i].i_FETCHEN())
        instr_router.o_MAP(instr_mem.i_INPUT(),
            RouterMapping(base=arch.inst_base, size=arch.inst_size), name='instr_mem')

        stack_router.o_MAP(stack_mem.i_INPUT(),
            RouterMapping(base=arch.stack_base, size=arch.stack_size), name='stack_mem')

        tcdm_port = 0
        for i in range(arch.num_core):
            # Fetch
            cores[i].o_FETCH(instr_router.i_INPUT(1 + 2 * i))

            # Scalar data
            cores[i].o_DATA(core_demux[i].i_INPUT())
            core_demux[i].o_MAP(tcdm.i_INPUT(tcdm_port),
                RouterMapping(base=arch.tcdm_base, size=arch.tcdm_size), name='tcdm')
            tcdm_port += 1
            core_demux[i].o_MAP(instr_router.i_INPUT(2 + 2 * i),
                RouterMapping(base=arch.inst_base, size=arch.inst_size, remove_base=False),
                name='instr')
            core_demux[i].o_MAP(stack_router.i_INPUT(i),
                RouterMapping(base=arch.stack_base, size=arch.stack_size, remove_base=False),
                name='stack')
            core_demux[i].o_MAP_DEFAULT(narrow_axi.i_INPUT(i), name='soc')

            # Vector data, straight to the TCDM crossbar
            for lane in range(arch.spatz_num_lane):
                cores[i].o_VLSU(lane, tcdm.i_INPUT(tcdm_port))
                tcdm_port += 1

            # Hardware barrier
            cores[i].o_BARRIER_REQ(csr.i_BARRIER_REQ(i))
            csr.o_BARRIER_ACK(cores[i].i_BARRIER_ACK())

        # Narrow crossbar
        narrow_axi.o_MAP(csr.i_INPUT(),
            RouterMapping(base=arch.reg_base, size=arch.reg_size), name='csr')
        narrow_axi.o_MAP_DEFAULT(self.i_NARROW_SOC(), name='soc')

        # iDMA
        cores[arch.num_core-1].o_OFFLOAD(idma.i_OFFLOAD())
        idma.o_OFFLOAD_GRANT(cores[arch.num_core-1].i_OFFLOAD_GRANT())
        idma.itf_bind('axi_read', wide_axi.i_INPUT(0), signature=IoV2Beat(arch.wide_width))
        idma.itf_bind('axi_write', wide_axi.i_INPUT(1), signature=IoV2Beat(arch.wide_width))
        wide_axi.o_MAP(tcdm.i_WIDE_INPUT(0),
            RouterMapping(base=arch.tcdm_base, size=arch.tcdm_size), name='tcdm')
        wide_axi.o_MAP_DEFAULT(self.i_WIDE_SOC(), name='soc')

        # Inputs from the NoC
        self.o_NARROW_INPUT(tcdm.i_INPUT(tcdm_port))
        self.o_WIDE_INPUT(tcdm.i_WIDE_INPUT(1))

        self.cores = cores

    def set_binary(self, binary):
        # Used by the gvrun flow to provide the binary after construction.
        self.loader.set_binary(binary)

    def handle_executable(self, binary):
        for core in self.cores:
            core.handle_executable(binary)

    # Narrow output of the cluster (to the SoC and the NoC)
    def i_NARROW_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_soc', signature=IoV2Beat(NARROW_WIDTH))

    def o_NARROW_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_soc', itf, signature=IoV2Beat(NARROW_WIDTH))

    # Narrow input from the NoC (remote TCDM accesses)
    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature=IoV2Beat(NARROW_WIDTH))

    def o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature=IoV2Beat(NARROW_WIDTH),
            composite_bind=True)

    # Wide input from the NoC (remote TCDM accesses by the iDMAs)
    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input', signature=IoV2Beat(self.wide_width))

    def o_WIDE_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_input', itf, signature=IoV2Beat(self.wide_width),
            composite_bind=True)

    # Wide output of the cluster (iDMA to the SoC and the NoC)
    def i_WIDE_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_soc', signature=IoV2Beat(self.wide_width))

    def o_WIDE_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_soc', itf, signature=IoV2Beat(self.wide_width))
