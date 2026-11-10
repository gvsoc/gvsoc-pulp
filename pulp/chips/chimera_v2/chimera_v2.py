#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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

"""Chimera virtual board on the io_v2 stack.

Same memory map, boot flow and software interface as the v1 ``chimera``
board: a CVA6 host (``cva6_v2``, running RV32 code) boots from its ROM, jumps to the
loaded binary through the Cheshire PLATFORM_ROM register, and starts the 5
Snitch clusters through the CLINT software interrupts. The clusters are the
io_v2 Snitch cluster of ``snitch_v2``.

Everything is io_v2: routers are ``interco.router_v2``, memories
``memory.memory_v3``, the CLINT ``cpu.clint_v2``, and the binary is loaded
through ``utils.loader.loader_v2``. The FLLs, SoC event unit, ITC and padframe
of the v1 board are left out: no address reaches them there, and nothing uses
their events.

Address map seen by the host and the clusters:

  0x0200_0000  host boot ROM           4 KiB
  0x0204_0000  CLINT                   1 MiB
  0x0300_0000  Cheshire registers      16 KiB
  0x0300_4000  stdout                  4 KiB
  0x1000_0000  host data memory        32 KiB
  0x1000_8000  host instruction memory 64 KiB
  0x3000_0000  Snitch boot ROM         4 KiB (wide, cluster fetches only)
  0x3000_1000  Chimera registers       4 KiB
  0x4000_0000  cluster i at 0x4000_0000 + i * 0x20_0000: TCDM, peripherals
               and zero memory (256 KiB decoded of each 2 MiB window)
  0x4800_0000  memory island           128 MiB

Accesses to the 4 KiB cluster boot ROM window (cluster base + 0x2_0000) and
DMA accesses to the cluster peripherals are not decoded by the cluster, which
sends them back out; do not use them.
"""

import os

import gvsoc.systree
from gvsoc.signature import IoV2Beat
from vp.clock_domain import Clock_domain
from memory.memory_v3 import Memory, MemoryV3Config
from interco.router_v2 import Router, RouterConfig, RouterMapping, KIND_BEAT, KIND_UNTIMED
from interco.limiter_v2 import Limiter, LimiterConfig
from utils.loader.loader_v2 import ElfLoader
from cpu.clint_v2 import ClintV2
from pulp.cpu.iss.cva6_v2 import Cva6V2
from pulp.cpu.iss.cva6_v2_config import Cva6V2Config
from pulp.stdout.stdout_v3_v2 import StdoutV2
from pulp.snitch.snitch_cluster.snitch_v2_cluster import ClusterArch, SnitchCluster
from pulp.chips.chimera_v2.cheshire_regs import CheshireRegs, CheshireRegsConfig
from pulp.chips.chimera_v2.chimera_regs import ChimeraRegs
from gvrun.parameter import TargetParameter
from gvrun.attribute import Tree


NB_CLUSTERS = 5
CLUSTER_BASE = 0x4000_0000
CLUSTER_STRIDE = 0x0020_0000
# Decoded part of a cluster window: TCDM, boot ROM window, peripherals, zero memory
CLUSTER_SIZE = 0x0004_0000
CLUSTER_ZERO_MEM_OFFSET = 0x0003_0000
CLUSTER_ZERO_MEM_SIZE = 0x0001_0000
NB_CORES_PER_CLUSTER = 9

SNITCH_ROM_BASE = 0x3000_0000
SNITCH_ROM_SIZE = 0x1000
MEM_ISLAND_BASE = 0x4800_0000
MEM_ISLAND_SIZE = 0x0800_0000

# Frequency of the RTC driving the CLINT mtime (30518 ns period in the chimera
# testbench)
RTC_FREQ = 32768


def cluster_base(id: int) -> int:
    return CLUSTER_BASE + id * CLUSTER_STRIDE


class ChimeraArch(Tree):
    """Architecture attributes: one Snitch cluster description per cluster."""

    def __init__(self, parent, name):
        super().__init__(parent, name)

        # Cluster i owns harts 1 + 9*i .. 9 + 9*i, the host is hart 0. The
        # cores boot from the Snitch boot ROM, which waits for their CLINT
        # software interrupt. The cluster alias (snitch_cluster.hjson
        # alias_region_enable, ALIAS_TCDM_BASE_ADDR in the SDK) makes every
        # cluster see its own TCDM at 0x1800_0000. The icaches cache the
        # memory island (the 512 KiB of its PMA rule in chimera_cluster.sv;
        # the Hyperbus one is not modeled).
        self.clusters = []
        for id in range(NB_CLUSTERS):
            self.clusters.append(ClusterArch(self, f'cluster_{id}', cluster_base(id),
                id * NB_CORES_PER_CLUSTER + 1, auto_fetch=True, boot_addr=SNITCH_ROM_BASE,
                nb_core_per_cluster=NB_CORES_PER_CLUSTER, alias_base=0x1800_0000,
                cacheable_base=MEM_ISLAND_BASE, cacheable_size=0x0008_0000))


class SnitchClusterGroup(gvsoc.systree.Component):
    """The 5 Snitch clusters with their narrow (64-bit) and wide (512-bit)
    crossbars and the Snitch boot ROM.

    The clusters reach each other on both crossbars. What is not a cluster
    goes to the safety island, on the narrow and on the wide SoC ports. The
    safety island reaches each cluster on its own narrow input.
    """

    def __init__(self, parent, name, arch: ChimeraArch):
        super().__init__(parent, name)

        rom = Memory(self, 'snitch_rom', config=MemoryV3Config(size=SNITCH_ROM_SIZE, latency=1,
            stim_file=self.get_file_path('pulp/chips/chimera/snitch/snitch_bootrom.bin')))

        # Inputs: the clusters (0..4), then the safety island, one per cluster
        # window (5..9)
        narrow_axi = Router(self, 'narrow_axi', config=RouterConfig(
            kind=KIND_BEAT, width=8, max_pending_bursts_per_input=4))
        # Inputs: the clusters (0..4), then the narrow to wide bridge (5)
        wide_axi = Router(self, 'wide_axi', config=RouterConfig(
            kind=KIND_BEAT, width=64, max_pending_bursts_per_input=4, axi_id_ordering=True))

        # Only the wide crossbar of a cluster decodes its zero memory: the
        # narrow accesses to it cross to the wide crossbar. A beat router
        # cannot bind another one of a different width directly, so the
        # crossing goes through an untimed bridge (the framework inserts the
        # beat adapters on both sides).
        narrow_wide_bridge = Router(self, 'narrow_wide_bridge',
            config=RouterConfig(kind=KIND_UNTIMED))
        narrow_wide_bridge.o_MAP_DEFAULT(wide_axi.i_INPUT(NB_CLUSTERS), name='wide')

        wide_axi.o_MAP(rom.i_INPUT(), RouterMapping(base=SNITCH_ROM_BASE, size=SNITCH_ROM_SIZE),
            name='snitch_rom')

        self.clusters = []
        for id in range(NB_CLUSTERS):
            base = cluster_base(id)
            cluster = SnitchCluster(self, f'cluster_{id}', arch.clusters[id])
            self.clusters.append(cluster)

            cluster.o_NARROW_SOC(narrow_axi.i_INPUT(id))
            cluster.o_WIDE_SOC(wide_axi.i_INPUT(id))

            narrow_axi.o_MAP(cluster.i_NARROW_INPUT(), RouterMapping(base=base,
                size=CLUSTER_ZERO_MEM_OFFSET, remove_base=False), name=f'cluster_{id}')
            narrow_axi.o_MAP(narrow_wide_bridge.i_INPUT(id), RouterMapping(
                base=base + CLUSTER_ZERO_MEM_OFFSET, size=CLUSTER_ZERO_MEM_SIZE,
                remove_base=False), name=f'cluster_{id}_zero_mem')
            # A cluster DMA reaches the TCDM and zero memory of the others
            wide_axi.o_MAP(cluster.i_WIDE_INPUT(), RouterMapping(base=base, size=CLUSTER_SIZE,
                remove_base=False), name=f'cluster_{id}')

            # The safety island to this cluster
            self.__o_NARROW_INPUT(id, narrow_axi.i_INPUT(NB_CLUSTERS + id))

            for core in range(NB_CORES_PER_CLUSTER):
                self.__o_SW_IRQ(id * NB_CORES_PER_CLUSTER + core, cluster.i_MSIP(core))

        narrow_axi.o_MAP_DEFAULT(self.i_NARROW_SOC(), name='soc')
        wide_axi.o_MAP_DEFAULT(self.i_WIDE_SOC(), name='soc')

    def handle_executable(self, binary):
        for cluster in self.clusters:
            cluster.handle_executable(binary)

    def i_SW_IRQ(self, core: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'sw_irq_{core}', signature='wire<bool>')

    def __o_SW_IRQ(self, core: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'sw_irq_{core}', itf, signature='wire<bool>', composite_bind=True)

    def i_NARROW_INPUT(self, cluster: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'narrow_input_{cluster}', signature=IoV2Beat(8))

    def __o_NARROW_INPUT(self, cluster: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'narrow_input_{cluster}', itf, signature=IoV2Beat(8),
            composite_bind=True)

    def i_NARROW_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_soc', signature=IoV2Beat(8))

    def o_NARROW_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_soc', itf, signature=IoV2Beat(8))

    def i_WIDE_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_soc', signature=IoV2Beat(64))

    def o_WIDE_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_soc', itf, signature=IoV2Beat(64))


class SafetyIsland(gvsoc.systree.Component):
    """The host, its memories and peripherals, the memory island and the
    cluster group."""

    def __init__(self, parent, name, arch: ChimeraArch):
        super().__init__(parent, name)

        TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        #
        # Components
        #

        # The host, a CVA6 running RV32 code (the chimera-sdk host ISA), with
        # the standard RISC-V interrupts driven by the CLINT (msip /
        # mtimecmp). Bare-metal, without MMU.
        host = Cva6V2(self, 'host', config=Cva6V2Config(isa='rv32imc', boot_addr=0x0200_0000,
            mmu=False))

        loader = ElfLoader(self, 'loader')

        rom = Memory(self, 'rom', config=MemoryV3Config(size=0x1000, latency=0,
            stim_file=self.get_file_path('pulp/chips/chimera/bootrom.bin')))
        data_mem = Memory(self, 'data_memory', config=MemoryV3Config(size=0x8000, latency=0))
        inst_mem = Memory(self, 'inst_memory', config=MemoryV3Config(size=0x10000, latency=0))
        mem_island = Memory(self, 'memory_island', config=MemoryV3Config(size=MEM_ISLAND_SIZE,
            latency=0, atomics=True))

        # Hart 0 is the host, harts 1..45 the cluster cores
        nb_cluster_cores = NB_CLUSTERS * NB_CORES_PER_CLUSTER
        clint = ClintV2(self, 'clint', nb_cores=1 + nb_cluster_cores, frequency=RTC_FREQ)
        cheshire_regs = CheshireRegs(self, 'cheshire_regs', config=CheshireRegsConfig(
            rtc_freq=RTC_FREQ))
        chimera_regs = ChimeraRegs(self, 'chimera_regs')
        stdout = StdoutV2(self, 'stdout')

        clusters = SnitchClusterGroup(self, 'snitch_cluster_group', arch)

        # Inputs: host fetch (0), host data (1), loader (2), clusters narrow
        # (3) and wide (4)
        ico = Router(self, 'ico', config=RouterConfig(kind=KIND_UNTIMED))

        #
        # Bindings
        #

        host.o_FETCH(ico.i_INPUT(0))
        host.o_DATA(ico.i_INPUT(1))
        self.bind(host, 'time', clint, 'time')
        loader.o_OUT(ico.i_INPUT(2))
        loader.o_START(host.i_FETCHEN())
        # The boot ROM jumps to the entry point, read from PLATFORM_ROM
        loader.o_ENTRY(cheshire_regs.i_ENTRY())
        clusters.o_NARROW_SOC(ico.i_INPUT(3))
        clusters.o_WIDE_SOC(ico.i_INPUT(4))

        ico.o_MAP(rom.i_INPUT(), RouterMapping(base=0x0200_0000, size=0x1000), name='rom')
        ico.o_MAP(clint.i_INPUT(), RouterMapping(base=0x0204_0000, size=0x0010_0000),
            name='clint')
        ico.o_MAP(cheshire_regs.i_INPUT(), RouterMapping(base=0x0300_0000, size=0x4000),
            name='cheshire_regs')
        ico.o_MAP(stdout.i_INPUT(), RouterMapping(base=0x0300_4000, size=0x1000), name='stdout')
        ico.o_MAP(data_mem.i_INPUT(), RouterMapping(base=0x1000_0000, size=0x8000),
            name='data_memory')
        ico.o_MAP(inst_mem.i_INPUT(), RouterMapping(base=0x1000_8000, size=0x10000),
            name='inst_memory')
        ico.o_MAP(chimera_regs.i_INPUT(), RouterMapping(base=0x3000_1000, size=0x1000),
            name='chimera_regs')
        ico.o_MAP(mem_island.i_INPUT(), RouterMapping(base=MEM_ISLAND_BASE,
            size=MEM_ISLAND_SIZE), name='memory_island')
        # The TCDM crossbar of a cluster takes requests within one 8-byte
        # word: the requests to the clusters, like the loader ones, are cut
        # to the narrow bus width
        for id in range(NB_CLUSTERS):
            limiter = Limiter(self, f'cluster_{id}_limiter', config=LimiterConfig(bandwidth=8))
            ico.o_MAP(limiter.i_INPUT(), RouterMapping(base=cluster_base(id),
                size=CLUSTER_SIZE, remove_base=False), name=f'cluster_{id}')
            limiter.o_OUTPUT(clusters.i_NARROW_INPUT(id))

        self.bind(clint, 'sw_irq_0', host, 'msi')
        self.bind(clint, 'timer_irq_0', host, 'mti')
        for core in range(nb_cluster_cores):
            clint.o_SW_IRQ(core + 1, clusters.i_SW_IRQ(core))

        self.loader = loader
        self.host = host
        self.clusters = clusters
        self.register_binary_handler(self.handle_binary)

    def configure(self):
        # The binary comes from a parameter, set either from the command line
        # or by the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)
            # The cores take their debug symbols and HTIF symbols from the
            # executables. Binaries built by gvrun register themselves, the
            # others, like the ones given through the binary parameter, are
            # registered here.
            if os.environ.get('USE_GVRUN2') is None:
                self.host.handle_executable(binary)
                self.clusters.handle_executable(binary)
            else:
                from gvrun.systree import ExecutableContainer
                registered = [exe.get_binary() for exe in self.get_executables()]
                if binary not in registered:
                    self.add_executable(ExecutableContainer(binary))

    def handle_binary(self, binary: str):
        # Called when an executable is attached to a hierarchy of components
        # containing this one
        self.set_parameter('binary', binary)


class ChimeraBoard(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super().__init__(parent, name, options=options)

        arch = ChimeraArch(self, 'chimera')
        self.set_attributes(arch)

        clock = Clock_domain(self, 'clock', frequency=50_000_000)
        safety_island = SafetyIsland(self, 'safety_island', arch)
        self.bind(clock, 'out', safety_island, 'clock')
