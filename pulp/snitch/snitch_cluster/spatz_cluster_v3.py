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

"""Spatz cluster, v3 generation — full io_v2 stack.

More accurate model of ``tests/spatz/spatz-rtl`` than the v2 cluster:

- L1 TCDM: deny/retry ``spatz_tcdm_interco`` crossbar over 16 x 8 KiB
  ``memory_v3`` banks (RTL geometry: word-interleaved ``stream_xbar``,
  one master per bank per cycle, 1-cycle response latency, conflict
  stalls). The 11 narrow masters are the RTL's 10 core TCDM ports
  (per core: 4 Spatz VLSU + 1 Snitch scalar) plus the SoC AXI input;
  the DMA enters through a wide (superbank) input with priority over
  the cores, mirroring ``mem_wide_narrow_mux``.
- Cores: iss_v2 Spatz with the io_v2 VLSU (``vlsu_v2=True``) and the
  RTL outstanding depth (``num_spatz_outstanding_loads = 4``).
- Fabric: ``interco.router_v2`` routers (beat-streaming on the wide
  512-bit and narrow 64-bit AXI, matching per-cycle arbitration and
  bounded outstanding transactions).
- iDMA: io_v2 ``SnitchDmaV2`` (Snitch offload front-end, 2D middle-end,
  beat AXI back-end), looping back to the TCDM through the wide router
  like the RTL.
- Instruction caches: per-core private L0 (8 x 32 B lines) + shared
  L1 (4 KiB, 2 ways) built from ``cache.cache_v4``, refilling through
  the wide router.
- Peripherals: io_v2 Spatz cluster registers (HW barrier, CLINT,
  bootaddr) and zero memory.
"""

import math

import gvsoc.systree
from gvsoc.signature import IoV2Beat, IoV2SingleReq
from pulp.cpu.iss.spatz import Spatz
from pulp.cpu.iss.spatz_config import SpatzConfig
import memory.memory_v3 as memory_v3
from memory.memory_v3 import MemoryV3Config
import interco.router_v2 as router_v2
from interco.router_v2 import RouterConfig, RouterMapping, KIND_UNTIMED, KIND_BEAT
from pulp.snitch.snitch_cluster.spatz.spatz_tcdm_interco import (
    SpatzTcdmInterco, SpatzTcdmIntercoConfig)
import cache.cache_v4 as cache_v4
from cache.cache_v4 import CacheConfig
from pulp.snitch.zero_mem_v2 import ZeroMem
import pulp.snitch.snitch_cluster.spatz.cluster_registers_v2 as cluster_registers_v2
from ips.pulp.idma_v2.snitch_dma import SnitchDmaV2

from gvrun.attribute import Tree, Area


class ClusterArch(Tree):
    """Architectural description of the v3 Spatz cluster.

    Matches the RTL ``spatz_cluster.default.dram.hjson`` configuration:
    2 cores (core 0 owns the iDMA), 128 KiB TCDM in 16 word-interleaved
    banks of 8 B, 4 Spatz FUs / memory ports per core, VLEN 512.
    """

    def __init__(self, parent, name, base, first_hartid, auto_fetch=False,
            boot_addr=0x0000_1000, nb_core_per_cluster=2, spatz_nb_lanes=4,
            isa='rv32imfdcav'):
        super().__init__(parent, name)

        self.nb_core = nb_core_per_cluster
        self.base = base
        self.first_hartid = first_hartid

        self.boot_addr = boot_addr
        self.auto_fetch = auto_fetch
        self.barrier_irq = 19

        self.tcdm          = ClusterArch.Tcdm(self, 'tcdm', base,
            nb_masters=self.nb_core * (spatz_nb_lanes + 1) + 1)
        self.peripheral    = Area(self, 'peripheral', base + 0x0002_0000, 0x0001_0000, 'peripheral range')
        self.zero_mem      = Area(self, 'zero_mem', base + 0x0003_0000, 0x0001_0000, 'zero mem range')
        self.spatz_nb_lanes = spatz_nb_lanes
        # RTL num_spatz_outstanding_loads
        self.spatz_nb_outstanding_reqs = 4
        self.isa = isa

    class Tcdm(Tree):
        def __init__(self, parent, name, base, nb_masters):
            super().__init__(parent, name)
            self.area = Area(self, 'tcdm', base + 0x0000_0000, 0x0002_0000, 'TCDM range')
            # RTL: NrBanks = 16, DataWidth = 64 bits, TCDMDepth = 1024 words
            self.nb_banks = 16
            self.bank_width = 8
            self.bank_size = self.area.size // self.nb_banks
            # RTL MemoryMacroLatency: 1 cycle between grant and response
            self.latency = 1
            self.nb_masters = nb_masters


class SpatzClusterTcdm(gvsoc.systree.Component):
    """TCDM subsystem: SpatzTcdmInterco crossbar + memory_v3 banks.

    The crossbar arbitrates one master per bank per cycle with a
    round-robin rotor and the deny/retry io_v2 handshake — a conflict
    costs the loser one cycle per retry, like the RTL ``stream_xbar``
    with ``q_ready`` deassertion. The DMA enters through the wide input
    which claims all the banks spanned by an access within one tick,
    with priority over the narrow masters.

    The banks answer inline (IoV2Sync) with a fixed latency annotation
    and truncate=True: the cluster keeps absolute addresses on the TCDM
    paths (the TCDM base is size-aligned so the bank-local truncation
    is exact), which spares a remapping stage on the critical path.
    """

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        ico = SpatzTcdmInterco(self, 'ico', config=SpatzTcdmIntercoConfig(
            nb_masters=arch.nb_masters,
            nb_slaves=arch.nb_banks,
            interleaving_width=int(math.log2(arch.bank_width)),
            nb_wide_masters=1))

        for i in range(0, arch.nb_banks):
            bank = memory_v3.Memory(self, f'bank_{i}', config=MemoryV3Config(
                size=arch.bank_size, atomics=True, latency=arch.latency))
            ico.o_OUTPUT(i, bank.i_INPUT())

        for i in range(0, arch.nb_masters):
            self.bind(self, f'input_{i}', ico, f'input_{i}')
        self.bind(self, 'wide_input_0', ico, 'wide_input_0')

    def i_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        # Boundary forward to the TCDM interco's single-req input (DENY/retry
        # + inline DONE, single-beat responses).
        return gvsoc.systree.SlaveItf(self, f'input_{port}',
            signature=IoV2SingleReq())

    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        # Boundary forward to the TCDM interco's wide single-req input.
        return gvsoc.systree.SlaveItf(self, 'wide_input_0',
            signature=IoV2SingleReq())


class SnitchCluster(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, entry=0):
        super().__init__(parent, name)

        # Core 0 owns the iDMA (RTL Xdma one-hot)
        dma_core = 0

        #
        # Components
        #

        # Wide 512-bit and narrow 64-bit cluster crossbars. Beat streaming
        # gives per-cycle arbitration with burst atomicity; the RTL AXI
        # xbars register all ports (XbarLatency = CUT_ALL_PORTS) and allow
        # 4 outstanding transactions per port.
        wide_axi = router_v2.Router(self, 'wide_axi', config=RouterConfig(
            kind=KIND_BEAT, width=64, max_pending_bursts_per_input=4))
        narrow_axi = router_v2.Router(self, 'narrow_axi', config=RouterConfig(
            kind=KIND_BEAT, width=8, max_pending_bursts_per_input=4))

        # L1 Memory
        tcdm = SpatzClusterTcdm(self, 'tcdm', arch.tcdm)

        # Zero memory
        zero_mem = ZeroMem(self, 'zero_mem', size=arch.zero_mem.size)

        # Instruction caches: per-core private L0 (RTL snitch_icache L0,
        # 8 lines of 32 B) in front of a shared L1 (256-bit lines, 64
        # entries, 2 ways = 4 KiB), refilling through the wide crossbar.
        l0_caches = []
        for core_id in range(0, arch.nb_core):
            l0_caches.append(cache_v4.Cache(self, f'l0_icache_{core_id}',
                config=CacheConfig(size=256, line_size=32, ways=1)))
        icache_refill_ico = router_v2.Router(self, 'icache_refill_ico',
            config=RouterConfig(kind=KIND_UNTIMED))
        l1_icache = cache_v4.Cache(self, 'l1_icache',
            config=CacheConfig(size=4096, line_size=32, ways=2, refill_latency=2,
                prefetch=True))

        # Cores
        cores = []
        cores_demux = []

        for core_id in range(0, arch.nb_core):
            config = SpatzConfig(isa=arch.isa, fetch_enable=arch.auto_fetch,
                boot_addr=arch.boot_addr, hart_id=arch.first_hartid + core_id,
                htif=True, nb_lanes=arch.spatz_nb_lanes, lane_width=8,
                vlsu_v2=True, nb_outstanding_reqs=arch.spatz_nb_outstanding_reqs,
                nb_ipus=1)
            cores.append(Spatz(self, f'pe{core_id}', config=config))
            # Per-core demux (RTL reqrsp_demux in spatz_cc): TCDM accesses
            # go straight to the crossbar, everything else exits on the
            # narrow AXI.
            cores_demux.append(router_v2.Router(self, f'pe{core_id}_demux',
                config=RouterConfig(kind=KIND_UNTIMED)))

        # Cluster peripherals
        cluster_registers = cluster_registers_v2.ClusterRegisters(self, 'cluster_registers',
            nb_cores=arch.nb_core, boot_addr=entry)

        # Cluster DMA (io_v2 iDMA with Snitch offload front-end). The AXI
        # width is the wide-crossbar width; TCDM accesses loop back through
        # the wide crossbar like in RTL.
        idma = SnitchDmaV2(self, 'idma', transfer_queue_size=8, burst_queue_size=8,
            axi_width=64)

        #
        # Bindings
        #

        # Narrow crossbar: per-core demux defaults + the SoC narrow input.
        self.o_NARROW_INPUT(narrow_axi.i_INPUT(arch.nb_core))
        narrow_axi.o_MAP(tcdm.i_INPUT(arch.nb_core * (arch.spatz_nb_lanes + 1)),
            RouterMapping(base=arch.tcdm.area.base, size=arch.tcdm.area.size,
                remove_base=False), name='tcdm')
        narrow_axi.o_MAP(cluster_registers.i_INPUT(),
            RouterMapping(base=arch.peripheral.base, size=arch.peripheral.size),
            name='periph')
        # Narrow accesses to the zero memory cross to the wide crossbar
        # (RTL DW-upsize path). A beat router cannot bind another one of a
        # different width directly, so the crossing goes through an untimed
        # bridge (the framework inserts the beat adapters on both sides).
        narrow_wide_bridge = router_v2.Router(self, 'narrow_wide_bridge',
            config=RouterConfig(kind=KIND_UNTIMED))
        narrow_axi.o_MAP(narrow_wide_bridge.i_INPUT(0),
            RouterMapping(base=arch.zero_mem.base, size=arch.zero_mem.size,
                remove_base=False), name='zero_mem')
        narrow_wide_bridge.o_MAP_DEFAULT(wide_axi.i_INPUT(3), name='wide')
        narrow_axi.o_MAP_DEFAULT(self.i_NARROW_SOC(), name='soc')

        # Wide crossbar: icache refills, DMA and the narrow zero-mem alias.
        wide_axi.o_MAP(tcdm.i_WIDE_INPUT(),
            RouterMapping(base=arch.tcdm.area.base, size=arch.tcdm.area.size,
                remove_base=False), name='tcdm')
        wide_axi.o_MAP(zero_mem.i_INPUT(),
            RouterMapping(base=arch.zero_mem.base, size=arch.zero_mem.size),
            name='zero_mem')
        wide_axi.o_MAP_DEFAULT(self.i_WIDE_SOC(), name='soc')

        # Icache hierarchy
        for core_id in range(0, arch.nb_core):
            cores[core_id].o_FETCH(l0_caches[core_id].i_INPUT())
            l0_caches[core_id].o_REFILL(icache_refill_ico.i_INPUT(core_id))
            cores[core_id].o_FLUSH_CACHE(l0_caches[core_id].i_FLUSH())
            l0_caches[core_id].o_FLUSH_ACK(cores[core_id].i_FLUSH_CACHE_ACK())
        icache_refill_ico.o_MAP_DEFAULT(l1_icache.i_INPUT(), name='l1')
        l1_icache.o_REFILL(wide_axi.i_INPUT(0))

        # DMA offload from core 0
        cores[dma_core].o_OFFLOAD(idma.i_OFFLOAD())
        idma.o_OFFLOAD_GRANT(cores[dma_core].i_OFFLOAD_GRANT())
        # Beat-fidelity DMA read/write channels onto the wide beat plane
        # (same 64-byte beat width on both sides: direct bind, no adapter).
        idma.itf_bind('axi_read', wide_axi.i_INPUT(1), signature=IoV2Beat(64))
        idma.itf_bind('axi_write', wide_axi.i_INPUT(2), signature=IoV2Beat(64))

        # Per-core memory ports
        tcdm_port = 0
        for core_id in range(0, arch.nb_core):
            # Spatz VLSU ports bind directly to the TCDM crossbar, like the
            # RTL where they speak the TCDM protocol natively.
            for port in range(0, arch.spatz_nb_lanes):
                cores[core_id].o_VLSU(port, tcdm.i_INPUT(tcdm_port))
                tcdm_port += 1

            # Scalar data port through the per-core demux
            cores[core_id].o_DATA(cores_demux[core_id].i_INPUT())
            cores_demux[core_id].o_MAP(tcdm.i_INPUT(tcdm_port),
                RouterMapping(base=arch.tcdm.area.base, size=arch.tcdm.area.size,
                    remove_base=False), name='tcdm')
            tcdm_port += 1
            # Peripheral accesses reach the cluster registers on a per-core
            # port so the barrier knows which core is loading HW_BARRIER.
            cores_demux[core_id].o_MAP(cluster_registers.i_CORE_INPUT(core_id),
                RouterMapping(base=arch.peripheral.base, size=arch.peripheral.size),
                name='periph')
            cores_demux[core_id].o_MAP_DEFAULT(narrow_axi.i_INPUT(core_id), name='soc')

        # Fetch enable and IRQs
        for core_id in range(0, arch.nb_core):
            self.__o_FETCHEN(cores[core_id].i_FETCHEN())
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

    # The narrow/wide SoC crossings are beat-native on both sides (the
    # cluster and SoC routers have the same widths), so the composite ports
    # carry the beat signatures — a legacy 'io_v2' string here would insert
    # a useless (and beat-stream-incompatible) adapter at each crossing.

    # Narrow input from the SoC (remote TCDM/peripheral accesses, loader)
    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature=IoV2Beat(8))

    def o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature=IoV2Beat(8), composite_bind=True)

    def i_NARROW_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_soc', signature=IoV2Beat(8))

    # Narrow output of the cluster
    def o_NARROW_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_soc', itf, signature=IoV2Beat(8))

    def i_WIDE_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_soc', signature=IoV2Beat(64))

    # Wide output of the cluster
    def o_WIDE_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_soc', itf, signature=IoV2Beat(64))
