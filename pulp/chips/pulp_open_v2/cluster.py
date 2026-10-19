# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#
# pulp-open cluster on io_v2 and iss_v2: 8 Ri5ky PEs, a word-interleaved
# 16-bank TCDM behind the HCI crossbar, the cluster control unit, the event
# unit, the cluster DMA (mchan), the cluster timer and the hierarchical
# instruction cache. Same memory map as the io v1 cluster
# (pulp/chips/pulp_open/cluster.json):
#
#   0x1000_0000 .. 0x1000_FFFF  L1 TCDM (64 KB, 16 word-interleaved banks)
#   0x1020_0000 .. 0x103F_FFFF  cluster peripherals
#     0x1020_0000  cluster control      0x1020_0400  timer
#     0x1020_0800  event unit           0x1020_1400  icache control
#     0x1020_1800  DMA (FC channel)
#     0x1020_4000  event unit per-core demux
#     0x1020_4400  DMA per-core demux
#   0x0000_0000 .. 0x003F_FFFF  cluster-local alias of the two above
#   everything else              forwarded out of the cluster (o_SOC)
#
# The L1 test-and-set alias of the v1 cluster (0x1010_0000) is not modelled.
#
# Topology: each PE has its own demux router, so its L1 traffic enters the
# HCI on a dedicated port. As the RTL core_demux, everything which is not the
# L1 or a per-core demux goes through the PE's peripheral path into the shared
# periph_ico, whose catch-all is the per2axi port of the beat router (soc_ico)
# towards o_SOC. The external slave (i_INPUT) enters through ext_ico, which
# owns HCI port nb_pe; the DMA owns the last HCI ports.
#
# The off-cluster path is the one of the RTL (pulp_cluster b6ae547), measured
# on its waveforms: core_demux FIFO (1 cycle), per2axi (1 each way), the
# cluster AXI crossbar (combinational), then the cluster to SoC axi_cdc (see
# o_SOC), 16 cycles from a core load to its data in the L2.

from __future__ import annotations

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2SingleReq, IoV2Beat
from interco.router_v2 import Router, RouterConfig, RouterMapping, KIND_UNTIMED, KIND_BEAT
from interco.remapper_v2 import Remapper, RemapperConfig
from interco.fifo_v2 import Fifo, FifoConfig
from memory.memory_v3 import Memory, MemoryV3Config
from pulp.cpu.iss.ri5ky import Ri5ky, Ri5kyNn, Ri5kyConfig
from ips.pulp.cluster.cluster_ctrl import ClusterCtrl, ClusterCtrlConfig
from ips.pulp.cluster.hci import Hci, HciConfig
from ips.pulp.cluster.cluster_icache import ClusterIcache, ClusterIcacheConfig
from ips.pulp.mchan.mchan import Mchan, MchanConfig, KIND_BEAT as MCHAN_KIND_BEAT
from ips.pulp.event_unit.v3.event_unit import EventUnit
from ips.pulp.timer.timer_v2 import Timer, TimerConfig


class PulpOpenV2ClusterConfig(Config):
    """pulp-open cluster: sizing and cluster-absolute address map."""

    nb_pe:        int = cfg_field(default=8, desc="Number of processing elements")
    isa:          str = cfg_field(default='rv32imfc', desc="Base ISA string of the PEs")
    pulpnn:       bool = cfg_field(default=False, desc="PEs with the PULP-NN ISA extension")

    base:         int = cfg_field(default=0x1000_0000, fmt="hex", desc="Cluster window base")
    size:         int = cfg_field(default=0x0040_0000, fmt="hex", desc="Cluster window size")

    ext_axi_width: int = cfg_field(default=8, desc="Cluster<->SoC AXI beat width in bytes")
    nb_dma_loc_ports: int = cfg_field(default=4, desc="Cluster-DMA word-wide TCDM ports")
    dma_global_queue_depth: int = cfg_field(default=16, desc=(
        "Cluster-DMA global command queue depth (2*NB_CORES in the RTL dmac_wrap)"))

    l1_base:      int = cfg_field(default=0x1000_0000, fmt="hex", desc="L1 TCDM base address")
    l1_size:      int = cfg_field(default=0x0001_0000, fmt="hex", desc="Total L1 TCDM size (bytes)")
    nb_l1_banks:  int = cfg_field(default=16,          desc="Number of word-interleaved L1 banks")

    periph_base:  int = cfg_field(default=0x1020_0000, fmt="hex", desc="Cluster peripheral region base")
    periph_size:  int = cfg_field(default=0x0020_0000, fmt="hex", desc="Cluster peripheral region size")

    cluster_ctrl_base: int = cfg_field(default=0x1020_0000, fmt="hex", desc="Cluster control unit base")
    cluster_ctrl_size: int = cfg_field(default=0x0000_0400, fmt="hex", desc="Cluster control unit size")
    timer_base:        int = cfg_field(default=0x1020_0400, fmt="hex", desc="Cluster timer base")
    timer_size:        int = cfg_field(default=0x0000_0400, fmt="hex", desc="Cluster timer size")
    event_unit_base:   int = cfg_field(default=0x1020_0800, fmt="hex", desc="Event unit register base")
    event_unit_size:   int = cfg_field(default=0x0000_0800, fmt="hex", desc="Event unit register size")
    icache_ctrl_base:  int = cfg_field(default=0x1020_1400, fmt="hex", desc="Cluster icache control base")
    icache_ctrl_size:  int = cfg_field(default=0x0000_0400, fmt="hex", desc="Cluster icache control size")
    mchan_base:        int = cfg_field(default=0x1020_1800, fmt="hex", desc="Cluster DMA (FC channel) base")
    mchan_size:        int = cfg_field(default=0x0000_0400, fmt="hex", desc="Cluster DMA (FC channel) size")
    event_unit_demux_base: int = cfg_field(default=0x1020_4000, fmt="hex", desc="Event unit demux base")
    event_unit_demux_size: int = cfg_field(default=0x0000_0400, fmt="hex", desc="Event unit demux size")
    mchan_demux_base:  int = cfg_field(default=0x1020_4400, fmt="hex", desc="Cluster DMA per-core demux base")
    mchan_demux_size:  int = cfg_field(default=0x0000_0400, fmt="hex", desc="Cluster DMA per-core demux size")

    # Event unit input events (pe/irq of the v1 cluster.json)
    dma_event:         int = cfg_field(default=8,  desc="DMA end-of-transfer event (dma_0)")
    dma_irq:           int = cfg_field(default=9,  desc="DMA end-of-transfer irq (dma_1)")
    timer_event_lo:    int = cfg_field(default=10, desc="Cluster timer low event (timer_0)")
    timer_event_hi:    int = cfg_field(default=11, desc="Cluster timer high event (timer_1)")
    dma_ext_event:     int = cfg_field(default=22, desc="DMA FC-channel event (dma_ext)")


class PulpOpenV2Cluster(Component):

    def __init__(self, parent: Component, name: str, config: PulpOpenV2ClusterConfig):
        super().__init__(parent, name, config=config)

        nb_pe = config.nb_pe
        self._soc_beat_width = config.ext_axi_width

        def rtr_cfg():
            # Untimed demux routers: address decode + forward. Per-bank
            # contention timing comes from the HCI crossbar.
            return RouterConfig(kind=KIND_UNTIMED)

        # Instances
        #
        # The core demux only grants a core's access to a peripheral or to the
        # SoC when its response is about to come back, so the core cannot
        # execute anything meanwhile, load or store alike (lsu_hold_on_async).
        # The TCDM grants at once and is not concerned (it answers
        # synchronously).
        core_class = Ri5kyNn if config.pulpnn else Ri5ky
        pes = [
            core_class(self, f'pe{i}', config=Ri5kyConfig(
                isa=config.isa, hart_id=i, fetch_enable=False,
                lsu_hold_on_async=True))
            for i in range(nb_pe)
        ]

        tcdm = [
            Memory(self, f'tcdm_bank{i}', config=MemoryV3Config(
                size=config.l1_size // config.nb_l1_banks, power_trigger=(i == 0)))
            for i in range(config.nb_l1_banks)
        ]

        dma_cfg = MchanConfig(nb_channels=nb_pe + 1,
                              tcdm_addr_width=config.l1_size.bit_length() - 1,
                              ext_width=config.ext_axi_width,
                              nb_loc_ports=config.nb_dma_loc_ports,
                              global_queue_depth=config.dma_global_queue_depth,
                              kind=MCHAN_KIND_BEAT)

        # nb_pe core masters, the external (SoC -> L1) master, then the DMA
        # word ports
        dma_loc_base = nb_pe + 1
        l1_ico = Hci(self, 'l1_ico', config=HciConfig(
            nb_masters=dma_loc_base + dma_cfg.nb_loc_ports,
            nb_slaves=config.nb_l1_banks,
            interleaving_width=2))

        periph_ico = Router(self, 'periph_ico', config=rtr_cfg())
        # Core-demux peripheral path, one per PE (RTL core_demux periph_FIFO):
        # a request FIFO whose state machine holds the core until the response
        # is back, so a PE has a single peripheral / off-cluster access in
        # flight. The request leaves one cycle after the core issued it (the
        # FIFO register). The response comes back through two registers, and
        # the core is granted on the first of them: that grant is what the
        # response stands for here (the core resumes on the next cycle, and the
        # data of a load is there one cycle after the grant, see
        # lsu_hold_on_async). A cluster peripheral gives its response the cycle
        # after the request, which the models answering inline do not account
        # for (done_latency). Same as the el1 cluster.
        pe_paths = [Fifo(self, f'pe{i}_path', config=FifoConfig(depth=1, latency=1,
                        resp_latency=1, done_latency=1))
                    for i in range(nb_pe)]
        # per2axi: the peripheral crossbar's single external slave turns the
        # cores' 32-bit off-cluster accesses into AXI transactions, one per
        # cycle, with a register on the request (AR/AW slice) and on the
        # response (R/B slice). Every core's L2 traffic funnels through it,
        # but it does not serialise them: its buffers hold one access per core.
        per2axi = Fifo(self, 'per2axi', config=FifoConfig(depth=nb_pe, latency=1,
                       resp_latency=1, max_outstanding=nb_pe))
        # Off-cluster interconnect (RTL cluster_bus axi_xbar): a beat-streaming
        # router. The DMA ext port streams its beats directly; the single-req
        # masters (per2axi, icache refills) get an adapter inserted by the
        # framework on their input. The RTL crossbar has no register on its
        # path: a read goes through in the cycle it arrives, write data one
        # cycle after its address.
        soc_ico    = Router(self, 'soc_ico', config=RouterConfig(
            kind=KIND_BEAT, width=config.ext_axi_width,
            max_pending_bursts_per_input=8,
            max_input_pending_size=0, combinational=True))
        ext_ico    = Router(self, 'ext_ico', config=rtr_cfg())

        cluster_ctrl = ClusterCtrl(self, 'cluster_ctrl', config=ClusterCtrlConfig(nb_core=nb_pe))
        event_unit = EventUnit(self, 'event_unit', nb_core=nb_pe)
        timer = Timer(self, 'timer', TimerConfig())
        dma = Mchan(self, 'dma', config=dma_cfg)
        # As the RTL (PRIVATE_ICACHE): 512 B L0 per core, a 4 KB L1.5 made of
        # one bank per core, each with a 4-entry refill tracker
        # (RefillTracker_4), and refills going to a free way first
        icache = ClusterIcache(self, 'icache', config=ClusterIcacheConfig(
            nb_cores=nb_pe, l0_size=512, l0_ways=4, l1_size=4096, l1_ways=4,
            l1_max_refills=4, l1_refill_banks=nb_pe, refill_free_way_first=True))

        pe_icos = [Router(self, f'pe{i}_ico', config=rtr_cfg()) for i in range(nb_pe)]
        # Cluster-local alias (L1 at 0, peripherals at 0x20_0000), folded back
        # onto the absolute map on each PE data port, since a slave accepts a
        # single master on io_v2.
        cluster_local_size = (config.periph_base - config.l1_base) + config.periph_size
        alias_remappers = [
            Remapper(self, f'pe{i}_alias', config=RemapperConfig(
                base=0, size=cluster_local_size, target_base=config.l1_base),
                signature=IoV2SingleReq())
            for i in range(nb_pe)
        ]

        # Bindings
        #

        # HCI -> TCDM banks
        for i in range(config.nb_l1_banks):
            l1_ico.o_OUTPUT(i, tcdm[i].i_INPUT())

        for i in range(nb_pe):
            pe = pes[i]
            pe_ico = pe_icos[i]

            pe.o_DATA(alias_remappers[i].i_INPUT())
            alias_remappers[i].o_OUTPUT(pe_ico.i_INPUT(0))
            pe.o_FETCH(icache.i_FETCH(i))

            pe_ico.o_MAP(l1_ico.i_INPUT(i),
                RouterMapping(base=config.l1_base, size=config.l1_size), name='l1')
            pe_ico.o_MAP(event_unit.i_DEMUX_IN(i),
                RouterMapping(base=config.event_unit_demux_base,
                              size=config.event_unit_demux_size), name='eu_demux')
            pe_ico.o_MAP(dma.i_INPUT(i),
                RouterMapping(base=config.mchan_demux_base,
                              size=config.mchan_demux_size), name='dma_demux')
            # Everything else (cluster peripherals, off-cluster) goes through
            # the PE's peripheral path into the shared peripheral crossbar,
            # which routes the unmapped addresses out to the SoC.
            pe_ico.o_MAP_DEFAULT(pe_paths[i].i_INPUT(), name='periph')
            pe_paths[i].o_OUTPUT(periph_ico.i_INPUT(i))

            cluster_ctrl.o_BOOTADDR(i, pe.i_ENTRY())
            cluster_ctrl.o_FETCHEN(i, pe.i_FETCHEN())

            event_unit.o_IRQ_REQ(i, pe.i_IRQ_REQ())
            pe.o_IRQ_ACK(event_unit.i_IRQ_ACK(i))

        # Cluster peripherals
        periph_ico.o_MAP(cluster_ctrl.i_INPUT(),
            RouterMapping(base=config.cluster_ctrl_base, size=config.cluster_ctrl_size),
            name='cluster_ctrl')
        periph_ico.o_MAP(icache.i_CTRL(),
            RouterMapping(base=config.icache_ctrl_base, size=config.icache_ctrl_size),
            name='icache_ctrl')
        periph_ico.o_MAP(event_unit.i_INPUT(),
            RouterMapping(base=config.event_unit_base, size=config.event_unit_size),
            name='event_unit')
        periph_ico.o_MAP(timer.i_INPUT(),
            RouterMapping(base=config.timer_base, size=config.timer_size),
            name='timer')
        periph_ico.o_MAP(dma.i_INPUT(nb_pe),
            RouterMapping(base=config.mchan_base, size=config.mchan_size),
            name='dma')

        # DMA data paths
        for p in range(dma_cfg.nb_loc_ports):
            dma.o_LOC(p, l1_ico.i_INPUT(dma_loc_base + p))
        dma.o_EXT(soc_ico.i_INPUT(1))

        icache.o_REFILL(soc_ico.i_INPUT(2))

        # Events: DMA and timer to every core, FC-channel DMA irq to the cores
        # and up to the SoC
        for i in range(nb_pe):
            dma.o_EVENT(i, event_unit.i_IN_EVENT(config.dma_event, i))
            dma.o_IRQ(i, event_unit.i_IN_EVENT(config.dma_irq, i))
            dma.o_EXT_IRQ(event_unit.i_IN_EVENT(config.dma_ext_event, i))
            timer.o_IRQ0(event_unit.i_IN_EVENT(config.timer_event_lo, i))
            timer.o_IRQ1(event_unit.i_IN_EVENT(config.timer_event_hi, i))
        dma.o_EXT_IRQ(self.__i_FC_IRQ())

        self.__o_REF_CLOCK(timer.i_REF_CLOCK())

        # Off-cluster traffic: the peripheral crossbar's catch-all is the
        # per2axi port, the first input of the off-cluster router, which exits
        # through o_SOC (-> SoC -> L2).
        periph_ico.o_MAP_DEFAULT(per2axi.i_INPUT(), name='soc')
        per2axi.o_OUTPUT(soc_ico.i_INPUT(0))
        soc_ico.o_MAP_DEFAULT(self.__i_SOC(), name='soc')

        cluster_ctrl.o_HCI_CTRL(l1_ico.i_CTRL())

        # External slave (SoC -> cluster TCDM and peripherals)
        self.__o_INPUT(ext_ico.i_INPUT())
        ext_ico.o_MAP(l1_ico.i_INPUT(nb_pe),
            RouterMapping(base=config.l1_base, size=config.l1_size), name='l1')
        ext_ico.o_MAP(periph_ico.i_INPUT(nb_pe),
            RouterMapping(base=config.periph_base, size=config.periph_size,
                          remove_base=False), name='periph')

    # -------- external ports --------

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2SingleReq())

    def __o_INPUT(self, itf: SlaveItf):
        self.itf_bind('input', itf, signature=IoV2SingleReq(), composite_bind=True)

    def i_REF_CLOCK(self) -> SlaveItf:
        return SlaveItf(self, 'ref_clock', signature='clock_gen')

    def __o_REF_CLOCK(self, itf: SlaveItf):
        self.itf_bind('ref_clock', itf, signature='clock_gen', composite_bind=True)

    def o_SOC(self, itf: SlaveItf):
        # The cluster AXI master crosses into the SoC clock domain through the
        # RTL axi_cdc: one common_cells cdc_fifo_gray per AXI channel, 8
        # entries each (LogDepth 3): the bridge in its AXI mode, reads and
        # writes crossing apart with 8 requests each. On the RTL waveforms,
        # with the two clocks in phase, a request or a response is out 4
        # cycles after it went in, each way: 3 for the gray-pointer
        # synchronizer and 1 for the spill register at the destination.
        self.itf_bind('soc', itf, signature=IoV2Beat(self._soc_beat_width),
                      clock_bridge=('cdc_fifo_gray_beh',
                                    {'k_src_per_dir': 1, 'k_dst_per_dir': 3, 'depth': 8,
                                     'axi_channels': True}))

    def __i_SOC(self) -> SlaveItf:
        return SlaveItf(self, 'soc', signature=IoV2Beat(self._soc_beat_width))

    # DMA FC-channel end-of-transfer irq (-> FC ITC)
    def o_FC_IRQ(self, itf: SlaveItf):
        self.itf_bind('fc_irq', itf, signature='wire<bool>')

    def __i_FC_IRQ(self) -> SlaveItf:
        return SlaveItf(self, 'fc_irq', signature='wire<bool>')
