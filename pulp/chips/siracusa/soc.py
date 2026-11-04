# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#
# Siracusa SoC on io_v2 and iss_v2, forked from the pulp-open_v2 one
# (pulp/chips/pulp_open_v2/soc.py): the FC (Ri5ky with Xf8 and Xfaux) and its
# interrupt controller, the L2 (two private banks and the interleaved shared
# banks), the APB peripherals, the two FC timers and the three PLLs. Same
# memory map as the io v1 Siracusa SoC:
#
#   0x1A10_0000  PLL soc / periph / cluster (0x10 each)
#   0x1A10_2000  uDMA                      0x1A10_4000  APB SoC control
#   0x1A10_6000  SoC event unit            0x1A10_9800  FC ITC
#   0x1A10_B000  FC timer 0                0x1A10_B800  FC timer 1
#   0x1A10_F000  stdout
#   0x1C00_0000  L2 private bank 0 (32 KB)
#   0x1C00_8000  L2 private bank 1 (32 KB)
#   0x1C01_0000  L2 shared (1984 KB, 4 word-interleaved banks)
#   0x0000_0000  FC data alias of the L2 private bank 0 (32 KB)
#
# As on pulp-open_v2, the GPIOs, the debug TAPs and debug ROM, the PMU, the
# AXI proxy and the FC instruction cache and its controller are not modelled:
# the FC fetches words straight from the L2. The uDMA has its UART and
# HyperBus, at the Siracusa peripheral ids.
#
# Interconnect, as on pulp-open_v2: the cluster and the loader go through an
# AXI beat router and are cut into 32-bit lanes; each FC port and each lane
# has a demux which sends the shared L2 to its own port of the L2 log
# interconnect and everything else to the bridge router (private L2, APB,
# cluster).
# The cluster is an AXI slave of the AXI beat router, which the bridge router
# reaches for the cluster window.

from __future__ import annotations

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2SingleReq, IoV2Beat
from gvrun.parameter import TargetParameter
from gvrun.systree import ExecutableContainer
from interco.router_v2 import Router, RouterConfig, RouterMapping, KIND_BEAT, KIND_UNTIMED, KIND_AUTO
from interco.limiter_v2 import Limiter, LimiterConfig
from interco.rw_splitter_v2 import RwSplitter, RwSplitterConfig
from interco.splitter_v2 import Splitter, SplitterConfig
from interco.remapper_v2 import Remapper, RemapperConfig
from interco.log_ico_v2 import LogIco, LogIcoConfig
from memory.memory_v3 import Memory, MemoryV3Config
from utils.loader.loader_v2 import ElfLoader
from utils.io_v2_shared_clock_bridge import IoV2SharedClockBridge
from vp.clock_domain import ClockDomain, ClockDomainConfig
from pulp.cpu.iss.ri5ky import Ri5kyConfig
from pulp.chips.siracusa.cores import SiracusaRi5ky
from pulp.itc.itc_v1_v2 import Itc_v1_V2
from ips.pulp.udma.udma_v3 import UdmaV3, UdmaV3Config
from ips.pulp.hyper_cs.hyper_cs import HyperCs
from pulp.stdout.stdout_v3_v2 import StdoutV2
from ips.pulp.timer.timer_v2 import Timer, TimerConfig
from ips.pulp.soc_eu.soc_eu import SocEu, SocEuConfig
from ips.pulp.apb_soc_ctrl.apb_soc_ctrl import ApbSocCtrl
from ips.pulp.pll.siracusa_pll import SiracusaPll
from pulp.chips.siracusa.cluster import SiracusaCluster, SiracusaClusterConfig


class SiracusaSocConfig(Config):
    """Siracusa SoC: address map and FC interrupt lines."""

    fc_hart_id:      int = cfg_field(default=(31 << 5), desc="mhartid of the FC (cluster 31, core 0)")
    # The v1 Siracusa FC is rv32imc, but the smallfloat extensions of Ri5ky
    # need F (as on pulp-open_v2)
    fc_isa:          str = cfg_field(default='rv32imfc', desc="ISA string of the FC")

    l2_base:         int = cfg_field(default=0x1C00_0000, fmt="hex", desc="L2 base")
    l2_priv_size:    int = cfg_field(default=0x0000_8000, fmt="hex", desc="Size of each L2 private bank")
    l2_shared_base:  int = cfg_field(default=0x1C01_0000, fmt="hex", desc="L2 shared base")
    l2_shared_size:  int = cfg_field(default=0x001F_0000, fmt="hex", desc="L2 shared size")
    l2_shared_banks: int = cfg_field(default=4, desc="Number of word-interleaved L2 shared banks")
    l2_alias_size:   int = cfg_field(default=0x0000_8000, fmt="hex", desc=(
        "Size of the alias of the L2 private bank 0 at address 0, seen by the FC data port"))

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
    irq_timer1_lo:   int = cfg_field(default=12, desc="FC timer 1 low interrupt")
    irq_timer1_hi:   int = cfg_field(default=13, desc="FC timer 1 high interrupt")
    irq_clkref:      int = cfg_field(default=14, desc="Reference clock interrupt")
    soc_evt_ref_clock: int = cfg_field(default=56, desc="SoC event of the reference clock")

    cluster: SiracusaClusterConfig = cfg_field(init=False, desc="Cluster")

    def __post_init__(self):
        super().__post_init__()
        self.cluster = SiracusaClusterConfig()


# APB peripherals: (name, base, size)
_APB_MAP = {
    'fll_soc':        (0x1A10_0000, 0x10),
    'fll_periph':     (0x1A10_0010, 0x10),
    'fll_cluster':    (0x1A10_0020, 0x10),
    'udma':           (0x1A10_2000, 0x2000),
    'apb_soc_ctrl':   (0x1A10_4000, 0x1000),
    'soc_eu':         (0x1A10_6000, 0x1000),
    'fc_itc':         (0x1A10_9800, 0x800),
    'fc_timer':       (0x1A10_B000, 0x800),
    'fc_timer_1':     (0x1A10_B800, 0x800),
    'stdout':         (0x1A10_F000, 0x1000),
}


class SiracusaSoc(Component):

    def __init__(self, parent: Component, name: str, config: SiracusaSocConfig):
        super().__init__(parent, name, config=config)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        cluster_cfg = config.cluster

        # Instances
        #
        loader = ElfLoader(self, 'loader')

        fc = SiracusaRi5ky(self, 'fc', config=Ri5kyConfig(
            isa=config.fc_isa, hart_id=config.fc_hart_id, fetch_enable=False))
        fc_itc = Itc_v1_V2(self, 'fc_itc')

        apb_soc_ctrl = ApbSocCtrl(self, 'apb_soc_ctrl')
        soc_eu = SocEu(self, 'soc_eu', config=SocEuConfig(ref_clock_event=config.soc_evt_ref_clock))
        timer = Timer(self, 'timer', TimerConfig())
        timer_1 = Timer(self, 'timer_1', TimerConfig())
        stdout = StdoutV2(self, 'stdout')
        # Siracusa uDMA: 12 peripheral areas, UART 0 at id 0 and the HyperBus
        # at id 4 with 2 transfer ids (common registers at id 6)
        udma = UdmaV3(self, 'udma', config=UdmaV3Config(nb_periphs=12, uart_id=0,
            hyper_id=4, hyper_nb_tran_ids=2))
        # The HyperBus pads of the uDMA: chip select 0 is the HyperRAM, 1 the
        # HyperFlash
        hyper_pads = HyperCs(self, 'hyper0_pads')
        # Siracusa has PLLs where pulp-open has FLLs (the v1 Siracusa model kept
        # the FLLs, the SoC one not driving the SoC clock)
        fll_soc = SiracusaPll(self, 'fll_soc')
        fll_periph = SiracusaPll(self, 'fll_periph')
        fll_cluster = SiracusaPll(self, 'fll_cluster')

        soc_clock = ClockDomain(self, 'soc_clock_domain', config=ClockDomainConfig(frequency=50000000))
        periph_clock = ClockDomain(self, 'periph_clock_domain', config=ClockDomainConfig(frequency=50000000))
        cluster_clock = ClockDomain(self, 'cluster_clock_domain', config=ClockDomainConfig(frequency=50000000))

        l2_priv0 = Memory(self, 'l2_priv0', config=MemoryV3Config(size=config.l2_priv_size, power_trigger=True))
        l2_priv1 = Memory(self, 'l2_priv1', config=MemoryV3Config(size=config.l2_priv_size))
        # The banks are not a power of 2 (496 KB), their offset must not be
        # truncated with size - 1
        l2_shared = [
            Memory(self, f'l2_shared_{i}', config=MemoryV3Config(
                size=config.l2_shared_size // config.l2_shared_banks, truncate=False))
            for i in range(config.l2_shared_banks)
        ]

        cluster = SiracusaCluster(self, 'cluster', config=cluster_cfg)

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
        # The RTL has no register between the cluster CDC and the AXI to L2
        # bridge (axi64_to_lint32), whose AR/AW/W and R/B single slices are
        # the only ones of the path: the router is combinational and the
        # slices are the register slice behind it (one cycle each way, reads
        # and writes apart, as the AXI channels).
        axi = Router(self, 'axi', config=RouterConfig(kind=KIND_BEAT, width=config.axi_width,
            max_pending_bursts_per_input=8, max_input_pending_size=8 * config.axi_width,
            combinational=True))
        axi_slice = IoV2SharedClockBridge(self, 'axi64_to_lint32',
            signature=IoV2Beat(config.axi_width))
        rw_splitter = RwSplitter(self, 'rw_splitter', config=RwSplitterConfig())
        axi_read_split = Splitter(self, 'axi_read_splitter', config=SplitterConfig(
            input_width=config.axi_width, output_width=4, lane_by_address=True))
        axi_write_split = Splitter(self, 'axi_write_splitter', config=SplitterConfig(
            input_width=config.axi_width, output_width=4, lane_by_address=True))
        fc_fetch_limiter = Limiter(self, 'fc_fetch_limiter', LimiterConfig(bandwidth=4))

        # The uDMA ports come last, the other ones keep the port they had
        masters = (['fc_data', 'fc_instr'] + [f'axi_read_{i}' for i in range(nb_axi_lanes)]
            + [f'axi_write_{i}' for i in range(nb_axi_lanes)] + ['udma_ro', 'udma_wo'])
        l2_shared_ico = LogIco(self, 'l2_shared_ico', config=LogIcoConfig(
            nb_masters=len(masters), nb_slaves=config.l2_shared_banks, interleaving_width=2))
        bridge = Router(self, 'bridge', config=RouterConfig())
        apb = Router(self, 'apb', config=RouterConfig(latency=config.apb_latency))
        # The AXI lanes of lint64_to_32 go straight into the L2 crossbar: no
        # buffering, a lost bank stalls the lane (and the bridge) until granted
        axi_lanes = [f'axi_{rw}_{i}' for rw in ('read', 'write') for i in range(nb_axi_lanes)]
        demuxes = {name: Router(self, f'{name}_demux', config=RouterConfig(
            kind=KIND_UNTIMED if name in axi_lanes else KIND_AUTO)) for name in masters}
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

        # uDMA: its L2 reads and writes, its events, and its pins
        udma.o_L2_RO(demuxes['udma_ro'].i_INPUT())
        udma.o_L2_WO(demuxes['udma_wo'].i_INPUT())
        udma.o_EVENT(soc_eu.i_EVENT_IN())
        udma.o_HYPER(0, hyper_pads.i_INPUT())
        for cs in range(2):
            self.bind(hyper_pads, f'cs{cs}', self, f'hyper0_cs{cs}')
        self.bind(udma, 'uart0', self, 'uart0')

        # AXI
        axi_limiter.o_OUTPUT(axi.i_INPUT(0))
        # SoC to cluster through the RTL axi_cdc, as the cluster to SoC way
        axi.o_MAP(cluster.i_INPUT(), mapping=RouterMapping(
            base=cluster_cfg.base, size=cluster_cfg.size, remove_base=False), name='cluster',
            clock_bridge=('cdc_fifo_gray_beh', {'k_src_per_dir': 1, 'k_dst_per_dir': 3,
                                                'depth': 8, 'axi_channels': True}))
        axi.o_MAP_DEFAULT(axi_slice.i_INPUT(), name='soc')
        axi_slice.o_OUTPUT(rw_splitter.i_INPUT())
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
        # The cluster is behind the AXI crossbar: the bridge reaches it through
        # the lint to AXI converter, the last input of the AXI router
        bridge.o_MAP(axi.i_INPUT(2), mapping=RouterMapping(
            base=cluster_cfg.base, size=cluster_cfg.size, remove_base=False,
            latency=config.axi_latency), name='cluster')

        # APB
        apb_slaves = {
            'fll_soc': fll_soc.i_INPUT(),
            'fll_periph': fll_periph.i_INPUT(),
            'fll_cluster': fll_cluster.i_INPUT(),
            'udma': udma.i_INPUT(),
            'apb_soc_ctrl': apb_soc_ctrl.i_INPUT(),
            'soc_eu': soc_eu.i_INPUT(),
            'fc_itc': fc_itc.i_INPUT(),
            'fc_timer': timer.i_INPUT(),
            'fc_timer_1': timer_1.i_INPUT(),
            'stdout': stdout.i_INPUT(),
        }
        for name, (base, size) in _APB_MAP.items():
            apb.o_MAP(apb_slaves[name], mapping=RouterMapping(base=base, size=size), name=name)

        # Interrupts
        timer.o_IRQ0(fc_itc.i_EVENT(config.irq_timer0_lo))
        timer.o_IRQ1(fc_itc.i_EVENT(config.irq_timer0_hi))
        timer_1.o_IRQ0(fc_itc.i_EVENT(config.irq_timer1_lo))
        timer_1.o_IRQ1(fc_itc.i_EVENT(config.irq_timer1_hi))
        soc_eu.o_FC_EVENT(fc_itc.i_SOC_EVENT())

        # Clocks: the PLLs drive the SoC, peripheral and cluster domains, the
        # reference clock clocks the timers
        fll_soc.o_CLOCK(soc_clock.i_CTRL())
        fll_periph.o_CLOCK(periph_clock.i_CTRL())
        fll_cluster.o_CLOCK(cluster_clock.i_CTRL())
        soc_clock.o_CLOCK(self.__i_CLOCK_SOC())
        cluster_clock.o_CLOCK(cluster.i_CLOCK())
        # The uDMA is clocked by the SoC clock, its peripherals send their bits
        # on the peripheral clock
        periph_clock.o_CLOCK(udma.i_PERIPH_CLOCK())

        for ref_clock_slave in [timer.i_REF_CLOCK(), timer_1.i_REF_CLOCK(),
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
