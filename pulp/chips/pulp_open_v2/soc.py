# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#
# pulp-open SoC on io_v2 and iss_v2: the FC (Ri5ky) with its instruction cache
# and interrupt controller, the L2 (two private banks and the interleaved
# shared banks), the APB peripherals and the three FLLs. Same memory map as the
# io v1 SoC (pulp/chips/pulp_open/soc.json):
#
#   0x1A10_0000  FLL soc / periph / cluster (0x10 each)
#   0x1A10_4000  APB SoC control           0x1A10_6000  SoC event unit
#   0x1A10_9800  FC ITC
#   0x1A10_B000  FC timer (4 KB window, 0x1A10_B800 aliases it as in the RTL)
#   0x1A10_F000  stdout
#   0x1C00_0000  L2 private bank 0 (32 KB)
#   0x1C00_8000  L2 private bank 1 (32 KB)
#   0x1C01_0000  L2 shared (512 KB, 4 word-interleaved banks)
#   0x0000_0000  FC data alias of 0x1C00_0000 (1 MB)
#
# Sizes and the FC fetch path follow the PULP RTL (pulp-platform/pulp): the FC
# has no instruction cache and fetches words straight from the L2.
#
# The uDMA and its peripherals, the GPIOs, the debug TAPs and the AXI proxy of
# the v1 SoC are not modelled, nor the FC instruction cache and its control
# registers of the v1 SoC, which the RTL does not have.
#
# Interconnect, as on GAP9: the cluster and the loader go through an AXI beat
# router and are cut into 32-bit lanes; each FC port and each lane has a demux
# which sends the shared L2 to its own port of the L2 log interconnect and
# everything else to the bridge router (private L2, APB, cluster).

from __future__ import annotations

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2SingleReq
from gvrun.parameter import TargetParameter
from gvrun.systree import ExecutableContainer
from interco.router_v2 import Router, RouterConfig, RouterMapping, KIND_BEAT
from interco.limiter_v2 import Limiter, LimiterConfig
from interco.rw_splitter_v2 import RwSplitter, RwSplitterConfig
from interco.splitter_v2 import Splitter, SplitterConfig
from interco.remapper_v2 import Remapper, RemapperConfig
from interco.log_ico_v2 import LogIco, LogIcoConfig
from memory.memory_v3 import Memory, MemoryV3Config
from utils.loader.loader_v2 import ElfLoader
from vp.clock_domain import ClockDomain, ClockDomainConfig
from pulp.cpu.iss.ri5ky import Ri5ky, Ri5kyConfig
from pulp.itc.itc_v1_v2 import Itc_v1_V2
from pulp.stdout.stdout_v3_v2 import StdoutV2
from ips.pulp.timer.timer_v2 import Timer, TimerConfig
from ips.pulp.soc_eu.soc_eu import SocEu, SocEuConfig
from ips.pulp.apb_soc_ctrl.apb_soc_ctrl import ApbSocCtrl
from ips.pulp.fll.fll_v1 import Fll
from pulp.chips.pulp_open_v2.cluster import PulpOpenV2Cluster, PulpOpenV2ClusterConfig


class PulpOpenV2SocConfig(Config):
    """pulp-open SoC: address map and FC interrupt lines."""

    fc_hart_id:      int = cfg_field(default=(31 << 5), desc="mhartid of the FC (cluster 31, core 0)")
    fc_isa:          str = cfg_field(default='rv32imfc', desc="ISA string of the FC")

    l2_base:         int = cfg_field(default=0x1C00_0000, fmt="hex", desc="L2 base")
    l2_priv_size:    int = cfg_field(default=0x0000_8000, fmt="hex", desc="Size of each L2 private bank")
    l2_shared_base:  int = cfg_field(default=0x1C01_0000, fmt="hex", desc="L2 shared base")
    l2_shared_size:  int = cfg_field(default=0x0008_0000, fmt="hex", desc="L2 shared size")
    l2_shared_banks: int = cfg_field(default=4, desc="Number of word-interleaved L2 shared banks")
    l2_alias_size:   int = cfg_field(default=0x0010_0000, fmt="hex", desc=(
        "Size of the alias of the L2 at address 0, seen by the FC data port"))

    apb_base:        int = cfg_field(default=0x1A10_0000, fmt="hex", desc="APB window base")
    apb_size:        int = cfg_field(default=0x0010_0000, fmt="hex", desc="APB window size")
    axi_width:       int = cfg_field(default=8, desc="SoC AXI width in bytes (cluster and loader masters)")

    # Latencies of the v1 SoC routers
    apb_latency:     int = cfg_field(default=8,  desc="APB router latency in cycles")
    axi_latency:     int = cfg_field(default=12, desc="Latency in cycles between the SoC and the cluster")

    # FC ITC lines (peripherals/fc_itc/irq of the v1 soc.json)
    irq_dma:         int = cfg_field(default=8,  desc="Cluster DMA FC-channel interrupt")
    irq_timer0_lo:   int = cfg_field(default=10, desc="FC timer 0 low interrupt")
    irq_timer0_hi:   int = cfg_field(default=11, desc="FC timer 0 high interrupt")
    irq_clkref:      int = cfg_field(default=14, desc="Reference clock interrupt")
    soc_evt_ref_clock: int = cfg_field(default=56, desc="SoC event of the reference clock")

    cluster: PulpOpenV2ClusterConfig = cfg_field(init=False, desc="Cluster")

    def __post_init__(self):
        super().__post_init__()
        self.cluster = PulpOpenV2ClusterConfig()


# APB peripherals: (name, base, size)
_APB_MAP = {
    'fll_soc':        (0x1A10_0000, 0x10),
    'fll_periph':     (0x1A10_0010, 0x10),
    'fll_cluster':    (0x1A10_0020, 0x10),
    'apb_soc_ctrl':   (0x1A10_4000, 0x1000),
    'soc_eu':         (0x1A10_6000, 0x1000),
    'fc_itc':         (0x1A10_9800, 0x800),
    'fc_timer':       (0x1A10_B000, 0x1000),
    'stdout':         (0x1A10_F000, 0x1000),
}


class PulpOpenV2Soc(Component):

    def __init__(self, parent: Component, name: str, config: PulpOpenV2SocConfig):
        super().__init__(parent, name, config=config)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        cluster_cfg = config.cluster

        # Instances
        #
        loader = ElfLoader(self, 'loader')

        fc = Ri5ky(self, 'fc', config=Ri5kyConfig(
            isa=config.fc_isa, hart_id=config.fc_hart_id, fetch_enable=False))
        fc_itc = Itc_v1_V2(self, 'fc_itc')

        apb_soc_ctrl = ApbSocCtrl(self, 'apb_soc_ctrl')
        soc_eu = SocEu(self, 'soc_eu', config=SocEuConfig(ref_clock_event=config.soc_evt_ref_clock))
        timer = Timer(self, 'timer', TimerConfig())
        stdout = StdoutV2(self, 'stdout')
        fll_soc = Fll(self, 'fll_soc')
        fll_periph = Fll(self, 'fll_periph')
        fll_cluster = Fll(self, 'fll_cluster')

        soc_clock = ClockDomain(self, 'soc_clock_domain', config=ClockDomainConfig(frequency=50000000))
        periph_clock = ClockDomain(self, 'periph_clock_domain', config=ClockDomainConfig(frequency=50000000))
        cluster_clock = ClockDomain(self, 'cluster_clock_domain', config=ClockDomainConfig(frequency=50000000))

        l2_priv0 = Memory(self, 'l2_priv0', config=MemoryV3Config(size=config.l2_priv_size, power_trigger=True))
        l2_priv1 = Memory(self, 'l2_priv1', config=MemoryV3Config(size=config.l2_priv_size))
        l2_shared = [
            Memory(self, f'l2_shared_{i}', config=MemoryV3Config(
                size=config.l2_shared_size // config.l2_shared_banks))
            for i in range(config.l2_shared_banks)
        ]

        cluster = PulpOpenV2Cluster(self, 'cluster', config=cluster_cfg)

        # Interconnect, as on GAP9 (gap9 soc.py and soc_interconnect.py).
        #
        # AXI: the loader and the cluster enter a beat router of the AXI width,
        # the loader through a limiter which cuts its writes into AXI beats.
        # Each beat leaves it as a single request, whose reads and writes are
        # separated and cut into 32-bit word lanes, one demux per lane.
        #
        # Every demux (FC data, FC instruction fetches, AXI lanes) sends the
        # shared L2 to its own port of the L2 log interconnect, which takes word
        # accesses, and everything else to the bridge router (private L2, APB,
        # cluster). The FC fetches reach their demux through a limiter which
        # cuts them into words.
        nb_axi_lanes = config.axi_width // 4
        axi_limiter = Limiter(self, 'axi_limiter', LimiterConfig(bandwidth=config.axi_width))
        axi = Router(self, 'axi', config=RouterConfig(kind=KIND_BEAT, width=config.axi_width,
            max_pending_bursts_per_input=8, max_input_pending_size=8 * config.axi_width))
        rw_splitter = RwSplitter(self, 'rw_splitter', config=RwSplitterConfig())
        axi_read_split = Splitter(self, 'axi_read_splitter', config=SplitterConfig(
            input_width=config.axi_width, output_width=4))
        axi_write_split = Splitter(self, 'axi_write_splitter', config=SplitterConfig(
            input_width=config.axi_width, output_width=4))
        fc_fetch_limiter = Limiter(self, 'fc_fetch_limiter', LimiterConfig(bandwidth=4))

        masters = (['fc_data', 'fc_instr'] + [f'axi_read_{i}' for i in range(nb_axi_lanes)]
            + [f'axi_write_{i}' for i in range(nb_axi_lanes)])
        l2_shared_ico = LogIco(self, 'l2_shared_ico', config=LogIcoConfig(
            nb_masters=len(masters), nb_slaves=config.l2_shared_banks, interleaving_width=2))
        bridge = Router(self, 'bridge', config=RouterConfig())
        apb = Router(self, 'apb', config=RouterConfig(latency=config.apb_latency))
        demuxes = {name: Router(self, f'{name}_demux', config=RouterConfig()) for name in masters}
        # The FC data port sees the L2 at address 0 too
        fc_alias = Remapper(self, 'fc_alias', config=RemapperConfig(
            base=0, size=config.l2_alias_size, target_base=config.l2_base),
            signature=IoV2SingleReq())

        # Bindings
        #

        # Loader
        loader.o_OUT(axi_limiter.i_INPUT())
        loader.o_START(fc.i_FETCHEN())
        loader.o_ENTRY(fc.i_ENTRY())

        # FC
        fc.o_DATA(fc_alias.i_INPUT())
        fc_alias.o_OUTPUT(demuxes['fc_data'].i_INPUT())
        fc.o_FETCH(fc_fetch_limiter.i_INPUT())
        fc_fetch_limiter.o_OUTPUT(demuxes['fc_instr'].i_INPUT())
        fc_itc.o_IRQ_REQ(fc.i_IRQ_REQ())
        fc.o_IRQ_ACK(fc_itc.i_IRQ_ACK())

        # Cluster
        cluster.o_SOC(axi.i_INPUT(1))
        cluster.o_FC_IRQ(fc_itc.i_EVENT(config.irq_dma))

        # AXI
        axi_limiter.o_OUTPUT(axi.i_INPUT(0))
        axi.o_MAP_DEFAULT(rw_splitter.i_INPUT(), name='soc')
        rw_splitter.o_READ_OUTPUT(axi_read_split.i_INPUT())
        rw_splitter.o_WRITE_OUTPUT(axi_write_split.i_INPUT())
        for i in range(nb_axi_lanes):
            axi_read_split.o_OUTPUT(i, demuxes[f'axi_read_{i}'].i_INPUT())
            axi_write_split.o_OUTPUT(i, demuxes[f'axi_write_{i}'].i_INPUT())

        # Demuxes: shared L2 to the log interconnect, the rest to the bridge
        for port, name in enumerate(masters):
            demuxes[name].o_MAP(l2_shared_ico.i_INPUT(port), mapping=RouterMapping(
                base=config.l2_shared_base, size=config.l2_shared_size), name='l2_shared')
            demuxes[name].o_MAP_DEFAULT(bridge.i_INPUT(port), name='bridge')

        for i in range(config.l2_shared_banks):
            l2_shared_ico.o_OUTPUT(i, l2_shared[i].i_INPUT())

        bridge.o_MAP(l2_priv0.i_INPUT(), mapping=RouterMapping(
            base=config.l2_base, size=config.l2_priv_size), name='l2_priv0')
        bridge.o_MAP(l2_priv1.i_INPUT(), mapping=RouterMapping(
            base=config.l2_base + config.l2_priv_size, size=config.l2_priv_size), name='l2_priv1')
        bridge.o_MAP(apb.i_INPUT(), mapping=RouterMapping(
            base=config.apb_base, size=config.apb_size, remove_base=False), name='apb')
        bridge.o_MAP(cluster.i_INPUT(), mapping=RouterMapping(
            base=cluster_cfg.base, size=cluster_cfg.size, remove_base=False,
            latency=config.axi_latency), name='cluster')

        # APB
        apb_slaves = {
            'fll_soc': fll_soc.i_INPUT(),
            'fll_periph': fll_periph.i_INPUT(),
            'fll_cluster': fll_cluster.i_INPUT(),
            'apb_soc_ctrl': apb_soc_ctrl.i_INPUT(),
            'soc_eu': soc_eu.i_INPUT(),
            'fc_itc': fc_itc.i_INPUT(),
            'fc_timer': timer.i_INPUT(),
            'stdout': stdout.i_INPUT(),
        }
        for name, (base, size) in _APB_MAP.items():
            apb.o_MAP(apb_slaves[name], mapping=RouterMapping(base=base, size=size), name=name)

        # Interrupts
        timer.o_IRQ0(fc_itc.i_EVENT(config.irq_timer0_lo))
        timer.o_IRQ1(fc_itc.i_EVENT(config.irq_timer0_hi))
        soc_eu.o_FC_EVENT(fc_itc.i_SOC_EVENT())

        # Clocks: the FLLs drive the SoC, peripheral and cluster domains, the
        # reference clock clocks the FLL control interfaces and the timers
        fll_soc.o_CLOCK(soc_clock.i_CTRL())
        fll_periph.o_CLOCK(periph_clock.i_CTRL())
        fll_cluster.o_CLOCK(cluster_clock.i_CTRL())
        soc_clock.o_CLOCK(self.__i_CLOCK_SOC())
        cluster_clock.o_CLOCK(cluster.i_CLOCK())

        for ref_clock_slave in [fll_soc.i_REF_CLOCK(), fll_periph.i_REF_CLOCK(),
                fll_cluster.i_REF_CLOCK(), timer.i_REF_CLOCK(),
                cluster.i_REF_CLOCK()]:
            self.__o_REF_CLOCK(ref_clock_slave)

        self.loader = loader
        self.register_binary_handler(self.handle_binary)

    def configure(self):
        # The binary comes from a parameter, set either from the command line or
        # by the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)
            # The cores take their trace symbols from the executables attached
            # to their hierarchy
            if not any(executable.get_binary() == binary
                       for executable in self.get_executables()):
                self.add_executable(ExecutableContainer(binary))

    def handle_binary(self, binary: str):
        self.set_parameter('binary', binary)

    # -------- ports --------

    def i_REF_CLOCK(self) -> SlaveItf:
        return SlaveItf(self, 'ref_clock', signature='clock_gen')

    def __o_REF_CLOCK(self, itf: SlaveItf):
        self.itf_bind('ref_clock', itf, signature='clock_gen', composite_bind=True)

    def o_CLOCK_SOC(self, itf: SlaveItf):
        self.itf_bind('clock_soc', itf, signature='clock')

    def __i_CLOCK_SOC(self) -> SlaveItf:
        return SlaveItf(self, 'clock_soc', signature='clock')
