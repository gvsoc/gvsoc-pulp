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

"""Snitch cluster on the io_v2 stack, with the iss_v2 Snitch core.

Model of the RTL ``snitch_cluster`` (default configuration):

- 8 compute cores and 1 DMA core (the last one), each an iss_v2
  :class:`SnitchV2` with its FP subsystem, FREP and 3 SSRs.
- L1 TCDM: 128 KiB in 32 word-interleaved banks of 64 bits (``memory_v3``,
  1-cycle latency) behind a deny/retry crossbar (``SpatzTcdmInterco``). Its
  26 narrow inputs are the RTL ones: per compute core, port 0 shared by the
  core data port, the FP LSU and SSR 0 (a per-core demux, which also sends
  everything outside the TCDM to the narrow AXI), and one port per SSR 1 and
  2; the DMA core data port; the narrow AXI.
- The data port of each core has a register on its request and one on its
  response, its FP subsystem one on its request (RTL RegisterCoreReq,
  RegisterCoreRsp, RegisterFPUReq), a cycle each on every data access.
- The cores reach the narrow AXI through one shared converter, as the RTL
  ``reqrsp_mux`` + ``reqrsp_to_axi``: 2 accesses in flight for all the cores
  (the mux response FIFO), at most 3 reads or 3 writes, and no read while
  writes are in flight nor the opposite (one AXI ID). The DMA enters on the wide
  input, with priority over the cores.
- Fabric: ``router_v2`` beat routers, wide 512-bit and narrow 64-bit, with
  4 outstanding transactions per input.
- iDMA: ``SnitchDmaV3`` (xdma front-end, ND mid-end, AXI read/write
  managers) offloaded from the DMA core.
- Instruction caches: per-core L0 (8 lines of 32 B) and a shared L1 (8 KiB,
  2 ways) refilling on the wide AXI.
- Peripherals: Snitch cluster registers (CLINT, barrier), zero memory.
"""

import math

import gvsoc.systree
from gvsoc.signature import IoV2Beat, IoV2SingleReq
from pulp.cpu.iss.snitch_v2 import SnitchV2, SnitchV2Config
import memory.memory_v3 as memory_v3
from memory.memory_v3 import MemoryV3Config
import interco.router_v2 as router_v2
from interco.fifo_v2 import Fifo, FifoConfig
from interco.router_v2 import RouterConfig, RouterMapping, KIND_UNTIMED, KIND_BEAT
from pulp.snitch.snitch_cluster.spatz.spatz_tcdm_interco import (
    SpatzTcdmInterco, SpatzTcdmIntercoConfig)
from pulp.snitch.snitch_icache import SnitchIcacheL0, SnitchIcacheL1
from pulp.snitch.zero_mem_v2 import ZeroMem
from pulp.snitch.snitch_cluster.cluster_registers_v2 import (ClusterRegisters,
    ClusterRegistersConfig)
from ips.pulp.idma_v3.snitch_dma import SnitchDmaV3
from ips.pulp.idma_v3.snitch_dma_config import SnitchDmaV3Config

from gvrun.attribute import Tree, Area, Value


class ClusterArch(Tree):
    """Architectural description of the Snitch cluster."""

    def __init__(self, parent, name, base, first_hartid, auto_fetch=False,
            boot_addr=0x0000_1000, nb_core_per_cluster=9, isa='rv32imfdca',
            peripheral_offset=0x0002_1000):
        super().__init__(parent, name)

        self.nb_core = nb_core_per_cluster
        self.base = base
        self.first_hartid = first_hartid

        self.boot_addr = boot_addr
        self.auto_fetch = auto_fetch
        self.barrier_irq = 19
        self.isa = isa
        # The last core is the DMA core, it has no SSR
        self.dma_core = self.nb_core - 1
        self.nb_ssrs = 3

        nb_masters = (self.nb_core - 1) * self.nb_ssrs + 1 + 1
        self.tcdm          = ClusterArch.Tcdm(self, 'tcdm', base, nb_masters)
        # RTL map: a 4 KiB cluster bootrom window at 0x2_0000 (not modeled,
        # the boot ROM is on the SoC), then the peripherals up to the zero
        # memory. Older clusters (Occamy) have no bootrom window and their
        # peripherals at 0x2_0000.
        self.peripheral    = Area(self, 'peripheral', base + peripheral_offset,
            0x0003_0000 - peripheral_offset, 'peripheral range')
        self.zero_mem      = Area(self, 'zero_mem', base + 0x0003_0000, 0x0001_0000, 'zero mem range')
        # The only region the PMA marks cacheable for the icaches: the DRAM
        self.cacheable     = Area(self, 'cacheable', 0x8000_0000, 0x8000_0000, 'icache cacheable range')
        # SERIAL_LOOKUP of the shared L1 icache, 0 in the Snitch cluster
        self.icache_serial_lookup = Value(self, 'icache_serial_lookup', 0, cast=int,
            description='L1 icache serial lookup (SERIAL_LOOKUP), 0 or 1')

    class Tcdm(Tree):
        def __init__(self, parent, name, base, nb_masters):
            super().__init__(parent, name)
            self.area = Area(self, 'tcdm', base + 0x0000_0000, 0x0002_0000, 'TCDM range')
            # RTL: 32 banks of 64 bits
            self.nb_banks = 32
            self.bank_width = 8
            self.bank_size = self.area.size // self.nb_banks
            self.latency = 1
            self.nb_masters = nb_masters


class SnitchClusterTcdm(gvsoc.systree.Component):
    """TCDM: deny/retry crossbar (one master per bank per cycle, round-robin,
    wide DMA input with priority) over memory_v3 banks. The banks keep
    absolute addresses (the TCDM base is size-aligned)."""

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        ico = SpatzTcdmInterco(self, 'ico', config=SpatzTcdmIntercoConfig(
            nb_masters=arch.nb_masters,
            nb_slaves=arch.nb_banks,
            interleaving_width=int(math.log2(arch.bank_width)),
            nb_wide_masters=1, rr_tree=True))

        for i in range(0, arch.nb_banks):
            bank = memory_v3.Memory(self, f'bank_{i}', config=MemoryV3Config(
                size=arch.bank_size, atomics=True, latency=arch.latency))
            ico.o_OUTPUT(i, bank.i_INPUT())

        for i in range(0, arch.nb_masters):
            self.bind(self, f'input_{i}', ico, f'input_{i}')
        self.bind(self, 'wide_input_0', ico, 'wide_input_0')

    def i_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'input_{port}', signature=IoV2SingleReq())

    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input_0', signature=IoV2SingleReq())


class SnitchCluster(gvsoc.systree.Component):

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        #
        # Components
        #

        wide_axi = router_v2.Router(self, 'wide_axi', config=RouterConfig(
            kind=KIND_BEAT, width=64, max_pending_bursts_per_input=4))
        narrow_axi = router_v2.Router(self, 'narrow_axi', config=RouterConfig(
            kind=KIND_BEAT, width=8, max_pending_bursts_per_input=4))

        tcdm = SnitchClusterTcdm(self, 'tcdm', arch.tcdm)

        zero_mem = ZeroMem(self, 'zero_mem', size=arch.zero_mem.size)

        # Instruction caches: per-core L0 (8 lines of 32 B) in front of the
        # shared L1 (8 KiB, 2 ways, 256-bit lines). As in the RTL, the
        # prefetcher is in the L0s (next line or the target of the first
        # taken branch / JAL of the line, one refill pending at a time, demand
        # miss or prefetch), and the L1 serves the L0s through one
        # port each, one lookup per cycle round-robin, with 2 refills in
        # flight (NUM_AXI_OUTSTANDING).
        l0_caches = []
        for core_id in range(0, arch.nb_core):
            l0_caches.append(SnitchIcacheL0(self, f'l0_icache_{core_id}',
                line_count=8, line_width=256, cacheable_base=arch.cacheable.base,
                cacheable_size=arch.cacheable.size))
        l1_icache = SnitchIcacheL1(self, 'l1_icache', line_count=128, way_count=2,
            line_width=256, nr_fetch_ports=arch.nb_core, num_axi_outstanding=2,
            serial_lookup=bool(arch.icache_serial_lookup), cacheable_base=arch.cacheable.base, cacheable_size=arch.cacheable.size)

        cores = []
        cores_demux = []
        for core_id in range(0, arch.nb_core):
            config = SnitchV2Config(isa=arch.isa, fetch_enable=arch.auto_fetch,
                boot_addr=arch.boot_addr, hart_id=arch.first_hartid + core_id, htif=True,
                nb_ssrs=arch.nb_ssrs)
            cores.append(SnitchV2(self, f'pe{core_id}', config=config))
            # Per-core TCDM port 0, shared by the core data port, the FP LSU
            # and SSR 0. Everything outside the TCDM exits on the narrow AXI.
            cores_demux.append(router_v2.Router(self, f'pe{core_id}_demux',
                config=RouterConfig(kind=KIND_UNTIMED)))

        # All the cores go to the narrow AXI through the RTL reqrsp_mux, an
        # in-order request channel elected by a lock-in rr_arb_tree, without
        # registers, and reqrsp_to_axi (on the mapping to the narrow AXI)
        cores_mux = router_v2.Router(self, 'cores_mux', config=RouterConfig(
            kind=KIND_BEAT, width=8, combinational=True, ordered_channel=True,
            rr_arb_tree=True, lock_in=True, max_pending_bursts=64))

        cluster_registers = ClusterRegisters(self, 'cluster_registers',
            config=ClusterRegistersConfig(nb_cores=arch.nb_core))

        # The Snitch cluster default config: 24 bursts in flight
        # (dma_axi_req_fifo_depth) and an 8-entry request FIFO
        # (dma_req_fifo_depth). The write meta FIFO derives from them. The
        # offload request goes through a register (RegisterOffloadReq).
        idma = SnitchDmaV3(self, 'idma', config=SnitchDmaV3Config(data_width=64,
            num_ax_in_flight=24, req_fifo_depth=8, meta_fifo_depth=0,
            offload_req_latency=1))

        #
        # Bindings
        #

        # Narrow crossbar: the cores through their mux + the SoC narrow input.
        # The mux holds 2 transactions in flight (its response FIFO, whose
        # full flag is a register), reqrsp_to_axi 3 reads or 3 writes
        # (MaxTrans 4), never both at once (a single AXI ID).
        cores_mux.o_MAP(narrow_axi.i_INPUT(0), RouterMapping(base=0, size=0,
            max_pending_bursts=2, full_registered=True, max_pending_per_dir=3,
            rw_exclusive=True), name='axi')
        self.o_NARROW_INPUT(narrow_axi.i_INPUT(1))
        narrow_axi.o_MAP(tcdm.i_INPUT(arch.tcdm.nb_masters - 1),
            RouterMapping(base=arch.tcdm.area.base, size=arch.tcdm.area.size,
                remove_base=False), name='tcdm')
        narrow_axi.o_MAP(cluster_registers.i_INPUT(),
            RouterMapping(base=arch.peripheral.base, size=arch.peripheral.size),
            name='periph')
        # As in the RTL, the narrow crossbar does not decode the zero memory,
        # only the DMA one does: the narrow accesses to its window leave the
        # cluster with everything else
        narrow_axi.o_MAP_DEFAULT(self.i_NARROW_SOC(), name='soc')

        # Wide crossbar: icache refills and DMA. The RTL crossbar registers
        # all its ports: 2 cycles through it each way, where the beat router
        # and its single-request adapter take 1. The cluster slaves get the
        # difference from a register stage (the testbench memory latency
        # already includes it for the SoC port).
        wide_tcdm_reg = Fifo(self, 'wide_tcdm_reg', config=FifoConfig(
            depth=8, latency=1, resp_latency=1, max_outstanding=8,
            done_annotated_latency=True))
        wide_zero_mem_reg = Fifo(self, 'wide_zero_mem_reg', config=FifoConfig(
            depth=8, latency=1, resp_latency=1, max_outstanding=8,
            done_annotated_latency=True))
        wide_axi.o_MAP(wide_tcdm_reg.i_INPUT(),
            RouterMapping(base=arch.tcdm.area.base, size=arch.tcdm.area.size,
                remove_base=False), name='tcdm')
        wide_tcdm_reg.o_OUTPUT(tcdm.i_WIDE_INPUT())
        wide_axi.o_MAP(wide_zero_mem_reg.i_INPUT(),
            RouterMapping(base=arch.zero_mem.base, size=arch.zero_mem.size),
            name='zero_mem')
        wide_zero_mem_reg.o_OUTPUT(zero_mem.i_INPUT())
        wide_axi.o_MAP_DEFAULT(self.i_WIDE_SOC(), name='soc')
        # Wide accesses from the SoC (other clusters' DMAs), left unbound on a
        # single-cluster SoC
        self.o_WIDE_INPUT(wide_axi.i_INPUT(4))

        # Icache hierarchy
        for core_id in range(0, arch.nb_core):
            cores[core_id].o_FETCH(l0_caches[core_id].i_INPUT())
            l0_caches[core_id].o_REFILL(l1_icache.i_INPUT(core_id))
            # fence.i flushes the core's own L0 and the whole shared L1, as
            # the RTL does (snitch_icache: flush_valid = |flush_valid_i)
            cores[core_id].o_FLUSH_CACHE(l0_caches[core_id].i_FLUSH())
            cores[core_id].o_FLUSH_CACHE(l1_icache.i_FLUSH())
            l0_caches[core_id].o_FLUSH_ACK(cores[core_id].i_FLUSH_CACHE_ACK())
        l1_icache.o_REFILL(wide_axi.i_INPUT(0))

        # DMA offload from the DMA core
        cores[arch.dma_core].o_OFFLOAD(idma.i_OFFLOAD())
        idma.o_OFFLOAD_GRANT(cores[arch.dma_core].i_OFFLOAD_GRANT())
        idma.o_AXI_READ(wide_axi.i_INPUT(1))
        idma.o_AXI_WRITE(wide_axi.i_INPUT(2))

        # Per-core memory ports
        tcdm_port = 0
        for core_id in range(0, arch.nb_core):
            demux = cores_demux[core_id]
            # The registers of the core and FP subsystem data ports, which
            # hold up to their 4 accesses in flight
            data_reg = Fifo(self, f'pe{core_id}_data_reg', config=FifoConfig(
                depth=4, latency=1, resp_latency=1, max_outstanding=4,
                done_annotated_latency=True))
            fp_data_reg = Fifo(self, f'pe{core_id}_fp_data_reg', config=FifoConfig(
                depth=4, latency=1, resp_latency=0, max_outstanding=4,
                done_annotated_latency=True))
            cores[core_id].o_DATA(data_reg.i_INPUT())
            data_reg.o_OUTPUT(demux.i_INPUT(0))
            cores[core_id].o_FP_DATA(fp_data_reg.i_INPUT())
            fp_data_reg.o_OUTPUT(demux.i_INPUT(1))
            demux.o_MAP(tcdm.i_INPUT(tcdm_port),
                RouterMapping(base=arch.tcdm.area.base, size=arch.tcdm.area.size,
                    remove_base=False), name='tcdm')
            demux.o_MAP_DEFAULT(cores_mux.i_INPUT(core_id), name='soc')
            tcdm_port += 1

            if core_id != arch.dma_core:
                cores[core_id].o_SSR(0, demux.i_INPUT(2))
                for ssr in range(1, arch.nb_ssrs):
                    cores[core_id].o_SSR(ssr, tcdm.i_INPUT(tcdm_port))
                    tcdm_port += 1

        # Fetch enable, barrier and IRQs
        for core_id in range(0, arch.nb_core):
            self.__o_FETCHEN(cores[core_id].i_FETCHEN())
            cores[core_id].o_BARRIER_REQ(cluster_registers.i_BARRIER_REQ(core_id))
            cluster_registers.o_BARRIER_ACK(cores[core_id].i_BARRIER_ACK())
            cluster_registers.o_EXTERNAL_IRQ(core_id, cores[core_id].i_IRQ(arch.barrier_irq))
            self.__o_MSIP(core_id, cores[core_id].i_IRQ(3))
            self.__o_MTIP(core_id, cores[core_id].i_IRQ(7))
            self.__o_MEIP(core_id, cores[core_id].i_IRQ(11))

        self.cores = cores

    def handle_executable(self, binary):
        for core in self.cores:
            core.handle_executable(binary)

    def i_MEIP(self, core: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'meip_{core}', signature='wire<bool>')

    def __o_MEIP(self, core: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'meip_{core}', itf, signature='wire<bool>', composite_bind=True)

    def i_MTIP(self, core: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'mtip_{core}', signature='wire<bool>')

    def __o_MTIP(self, core: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'mtip_{core}', itf, signature='wire<bool>', composite_bind=True)

    def i_MSIP(self, core: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'msip_{core}', signature='wire<bool>')

    def __o_MSIP(self, core: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'msip_{core}', itf, signature='wire<bool>', composite_bind=True)

    def i_FETCHEN(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'fetchen', signature='wire<bool>')

    def __o_FETCHEN(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('fetchen', itf, signature='wire<bool>', composite_bind=True)

    # The narrow/wide SoC crossings are beat-native on both sides (same
    # widths), so the composite ports carry the beat signatures.

    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature=IoV2Beat(8))

    def o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature=IoV2Beat(8), composite_bind=True)

    def i_NARROW_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_soc', signature=IoV2Beat(8))

    def o_NARROW_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_soc', itf, signature=IoV2Beat(8))

    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input', signature=IoV2Beat(64))

    def o_WIDE_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_input', itf, signature=IoV2Beat(64), composite_bind=True)

    def i_WIDE_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_soc', signature=IoV2Beat(64))

    def o_WIDE_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_soc', itf, signature=IoV2Beat(64))
