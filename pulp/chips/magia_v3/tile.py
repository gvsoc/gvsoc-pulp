# Copyright (C) 2025 Fondazione Chips-IT

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

"""magia_v3 tile, described with io_v2 models.

An io_v2 slave port binds **exactly one** master, so every fan-in of the tile
is explicit: the x-bars are routers with one input per master, the TCDM banks
sit behind a single crossbar with one input per master, and the event unit
sits behind a small router joining the direct link of the control core and
the OBI x-bar.

Off-tile traffic leaves through one catch-all mapping per x-bar instead of one
mapping per remote tile: same routing, one master port. An address that has no
mapping at all therefore goes off-tile instead of raising a decode error.
"""

import json
import math
from typing import List
import gvsoc.systree
import gdbserver.gdbserver

from memory.memory_v3 import Memory, MemoryV3Config
from interco.router_v2 import Router, RouterConfig, RouterMapping, KIND_BEAT, KIND_UNTIMED
from gvsoc.signature import IoV2Beat, IoV2SingleReq
from utils.io_v2_shared_clock_bridge import IoV2SharedClockBridge

from pulp.stdout.stdout_v3_v2 import StdoutV2
from pulp.cpu.iss.spatz import Spatz
from pulp.cpu.iss.spatz_config import SpatzConfig
from pulp.snitch.snitch_cluster.spatz.spatz_tcdm_interco import (SpatzTcdmInterco,
                                                                SpatzTcdmIntercoConfig)
from pulp.light_redmule.light_redmule_v2 import LightRedmule
from pulp.event_unit.event_unit_v3_v2 import Event_unit
from ips.pulp.idma_v3.axi_obi_dma import AxiObiDmaV3
from ips.pulp.idma_v3.axi_obi_dma_config import AxiObiDmaV3Config

from pulp.chips.magia_v3.arch import *
from pulp.chips.magia_v3.icache import MagiaIcache
from pulp.chips.magia_v3.ctrl_core.core import CV32CtrlCore
from pulp.chips.magia_v3.pulp_core.core import CV32PulpCore
from pulp.chips.magia_v3.fractal_sync_mm_ctrl.fractal_sync_mm_ctrl import FSync_mm_ctrl
from pulp.chips.magia_v3.idma_mm_ctrl.idma_mm_ctrl import iDMA_mm_ctrl
from pulp.chips.magia_v3.obi_cut.obi_cut import ObiCut
from pulp.chips.magia_v3.cluster_regs.cluster_regs import ClusterRegs


class MagiaTileTcdm(gvsoc.systree.Component):
    """Tile TCDM: the banks behind a single io_v2 crossbar.

    The crossbar is ``SpatzTcdmInterco``: a word-interleaved crossbar with
    round-robin arbitration, one master per bank per cycle, and wide masters
    which reserve every bank their access spans and take priority over the
    narrow ones. Bank conflicts are therefore modelled, and a denied master
    re-issues from its retry callback.

    The banks receive the address decoded by the crossbar, whose upper bits
    still hold the tile offset; the banks truncate it, since the bank size is
    a power of two and the tile offset falls above the bank depth.

    Narrow inputs: the OBI x-bar (stack window and local L1 window, two
    inputs since they were two mappings of the same port) and the Spatz VLSU
    ports. Wide inputs: the NoC wide channel, the OBI ports of the two iDMA
    channels and RedMulE.
    """

    # Narrow ports
    NARROW_OBI_STACK = 0
    NARROW_OBI_L1    = 1
    NARROW_SPATZ     = 2
    NB_SPATZ_VLSU    = 4
    NB_NARROW        = NARROW_SPATZ + (NB_SPATZ_VLSU if MagiaArch.SPATZ_ENABLE else 0)

    # Wide ports
    WIDE_NOC          = 0
    WIDE_IDMA_AXI2OBI = 1
    WIDE_IDMA_OBI2AXI = 2
    WIDE_REDMULE      = 3
    NB_WIDE           = 4

    def __init__(self, parent, name):
        super().__init__(parent, name)

        nb_banks = MagiaArch.N_MEM_BANKS
        bank_size = MagiaArch.N_WORDS_BANK * MagiaArch.BYTES_PER_WORD

        ico = SpatzTcdmInterco(self, 'ico', config=SpatzTcdmIntercoConfig(
            nb_masters=self.NB_NARROW,
            nb_slaves=nb_banks,
            interleaving_width=int(math.log2(MagiaArch.BYTES_PER_WORD)),
            nb_wide_masters=self.NB_WIDE,
            # RedMulE (HWPE branch) and the iDMA (DMA branch) reach the banks
            # through separate HCI branches, arbitrated bank by bank
            # (local_interconnect.sv), not through one shared wide port
            wide_shared_port=False))

        for i in range(nb_banks):
            bank = Memory(self, f'bank_{i}', config=MemoryV3Config(
                size=bank_size, atomics=True, latency=MagiaDSE.TILE_TCDM_LATENCY))
            ico.o_OUTPUT(i, bank.i_INPUT())

        for i in range(self.NB_NARROW):
            self.bind(self, f'narrow_input_{i}', ico, f'input_{i}')

        for i in range(self.NB_WIDE):
            self.bind(self, f'wide_input_{i}', ico, f'wide_input_{i}')

    def i_INPUT(self, id: int) -> gvsoc.systree.SlaveItf:
        """Narrow port ``id``."""
        return gvsoc.systree.SlaveItf(self, f'narrow_input_{id}', signature=IoV2SingleReq())

    def i_SNITCH_SPATZ(self, id: int) -> gvsoc.systree.SlaveItf:
        """Spatz VLSU port ``id``."""
        return self.i_INPUT(self.NARROW_SPATZ + id)

    def i_WIDE_INPUT(self, id: int) -> gvsoc.systree.SlaveItf:
        """Wide (bank-spanning) port ``id``."""
        return gvsoc.systree.SlaveItf(self, f'wide_input_{id}', signature=IoV2SingleReq())


class MagiaV3Tile(gvsoc.systree.Component):
    def ev_unit_config(self):
        event_unit_json = {
            "event_unit": {
                "version": "4",
                "config": {
                    "nb_core": 1,
                    # Measured on the RTL (control core parked on cv.elw):
                    # event -> grant 2 cycles (the wake-up latency), read
                    # data one cycle later, plus the eu-direct cut. The
                    # default of 6 is for the PULP cluster cores.
                    "wakeup_req_latency": 1,
                    "properties": {
                        "dispatch": {"size": 8},
                        "mutex": {"nb_mutexes": 0},
                        "barriers": {"nb_barriers": 0},
                        "soc_event": {"nb_fifo_events": 8, "fifo_event": 8},
                        "events": {
                            "dispatch": 8,
                            "mutex": 0,
                            "barrier": 0
                        }
                    }
                }
            }
        }
        json_data = json.dumps(event_unit_json)
        return json.loads(json_data)

    def __init__(self, parent, name, tree, parser, tid: int=0):
        super().__init__(parent, name)
        self.tid_name = f'tile-{tid}'

        #
        # Cores and instruction caches
        #

        # Control core
        core_cv32 = CV32CtrlCore(self, f'tile-{tid}-cv32-core', core_id=tid,
            mtvec_addr=MagiaArch.BOOT_ADDR)

        # Instruction cache of the control core (magia_tile_pkg i$ parameters):
        # fully associative 32 x 16 B L0, 32 sets x 32 ways x 16 B L1
        cv32_i_cache = MagiaIcache(self, f'tile-{tid}-cv32-icache', nb_cores=1,
            l0_size=512, l0_line_size=16, l0_ways=32,
            l1_size=16384, l1_line_size=16, l1_ways=32,
            l1_refill_latency=MagiaDSE.TILE_ICACHE_REFILL_LATENCY,
            l0_refill_latency=MagiaDSE.TILE_ICACHE_L0_REFILL_LATENCY_SERIAL,
            l1_rtl_lfsr=True)

        if MagiaArch.SPATZ_ENABLE:

            # Snitch Spatz boot rom file
            snitch_spatz_rom = Memory(self, 'snitch-spatz-rom', config=MemoryV3Config(
                size=MagiaArch.SPATZ_BOOTROM_SIZE, latency=0,
                stim_file=self.get_file_path(tree.romfile)))

            # Snitch Spatz core (fetch_enable is set to false as we control the boot sequence.
            # The core automatically starts from the rom and the corresponding boot address
            # as soon as we issue the fetch enable)
            config = SpatzConfig(isa="rv32imfdcav", fetch_enable=False,
                boot_addr=MagiaArch.SPATZ_BOOTROM_ADDR, hart_id=tid + tree.nb_clusters,
                htif=False, nb_lanes=MagiaTileTcdm.NB_SPATZ_VLSU, lane_width=4, vlsu_v2=True)

            snitch_spatz = Spatz(self, f'tile-{tid}-snitch-spatz', config=config)

            # Instruction cache of Spatz (i_spatz_cc_icache): fully associative
            # 8 x 32 B L0, 32 sets x 2 ways x 32 B L1, parallel lookup
            snitch_spatz_i_cache = MagiaIcache(self, f'tile-{tid}-snitch-spatz-icache',
                nb_cores=1, l0_size=256, l0_line_size=32, l0_ways=8,
                l1_size=2048, l1_line_size=32, l1_ways=2, l1_refill_latency=2,
                l0_refill_latency=MagiaDSE.TILE_ICACHE_L0_REFILL_LATENCY_PARALLEL,
                l1_rtl_lfsr=True)

        if MagiaArch.PULP_ENABLE:
            # PULP cluster cores
            pulp_cores:List[CV32PulpCore] = []
            for pulp_id in range(0,tree.nb_pulp_cores):
                pulp_cores.append(CV32PulpCore(self, f'tile-{tid}-pulp-cv32-core-{pulp_id}',core_id=tree.nb_clusters*2 + tid*tree.nb_pulp_cores + pulp_id))
                # reverse formula to get cluster id: x=pulp_id-2*tree.nb_clusters; cluster_id=x/tree.nb_pulp_cores
                # reverse formula to get local pulp id: x mod tree.nb_pulp_cores

            # Instruction cache of the cluster (magia_tile_pkg CLUSTER_* i$
            # parameters): per core a fully associative L0 of 32 * nb_cores x
            # 16 B lines, shared L1 of 32 * nb_cores sets x 32 ways x 16 B
            pulp_l0_lines = 32 * tree.nb_pulp_cores
            pulp_i_cache = MagiaIcache(self, f'tile-{tid}-pulp-icache',
                nb_cores=tree.nb_pulp_cores, l0_size=pulp_l0_lines * 16, l0_line_size=16,
                l0_ways=pulp_l0_lines,
                l1_size=32 * tree.nb_pulp_cores * 32 * 16, l1_line_size=16, l1_ways=32,
                l1_refill_latency=MagiaDSE.TILE_ICACHE_REFILL_LATENCY,
                l0_refill_latency=MagiaDSE.TILE_ICACHE_L0_REFILL_LATENCY_SERIAL,
            l1_rtl_lfsr=True)

        if MagiaArch.PULP_ENABLE or MagiaArch.SPATZ_ENABLE:
            # Cluster control registers
            cluster_regs = ClusterRegs(self, f'tile-{tid}-cluster-regs', nb_pulp_cores=tree.nb_pulp_cores)

        #
        # Memories and interconnects
        #

        # Data scratchpad
        l1_tcdm = MagiaTileTcdm(self, f'tile-{tid}-tcdm')

        # AXI and OBI x-bars. They stream beats at the narrow width, one beat
        # per cycle per channel, with round-robin arbitration between inputs
        # and a bounded number of outstanding bursts per input. As in the RTL
        # (axi_xbar, obi_xbar) they are combinational, and their registers are
        # the cuts around them, modelled as register slices (see cut()):
        #
        # - axi_xbar: LatencyMode CUT_ALL_PORTS, a cut on every slave and every
        #   master port;
        # - obi_xbar: an obi_cut on every master port and on the EXT (from the
        #   AXI x-bar) and Spatz slave ports, none on the core ones.
        #
        # Single-request masters (core data ports, cache refills, the loader)
        # cross onto them through the framework's adapters.
        tile_xbar = Router(self, f'tile-{tid}-axi-xbar', config=RouterConfig(
            kind=KIND_BEAT, width=MagiaArch.BYTES_PER_WORD, combinational=True,
            max_pending_bursts_per_input=MagiaDSE.TILE_AXI_XBAR_MAX_BURSTS))
        obi_xbar = Router(self, f'tile-{tid}-obi-xbar', config=RouterConfig(
            kind=KIND_BEAT, width=MagiaArch.BYTES_PER_WORD, combinational=True,
            max_pending_bursts_per_input=MagiaDSE.TILE_OBI_XBAR_MAX_BURSTS))
        narrow = IoV2Beat(MagiaArch.BYTES_PER_WORD)

        # The two iDMA channels share the wide channel of the NoC: the AXI to
        # OBI channel only reads (AR/R) and the OBI to AXI one only writes
        # (AW/W/B), as in the RTL where they own disjoint channels of the wide
        # AXI port. This beat router only joins them onto the single NoC
        # input, keeping the read and write channels independent. It is
        # combinational, as the axi_rw_join of the RTL, and the NoC network
        # interface behind it buffers what it receives.
        wide_xbar = Router(self, f'tile-{tid}-wide-xbar', config=RouterConfig(
            kind=KIND_BEAT, width=MagiaArch.TILE_WIDE_WIDTH, combinational=True,
            max_pending_bursts_per_input=MagiaDSE.TILE_WIDE_XBAR_MAX_BURSTS))

        # Data port of the control core: the event-unit window goes to the
        # event unit on a direct link, the rest to the OBI x-bar (RTL
        # core_data_demux_eu_direct)
        cv32_data_demux = Router(self, f'tile-{tid}-cv32-data-demux', config=RouterConfig(
            kind=KIND_UNTIMED))

        # Input of the event unit: the direct link of the control core and the
        # OBI x-bar for the other masters
        event_unit_ico = Router(self, f'tile-{tid}-event-unit-ico', config=RouterConfig(
            kind=KIND_UNTIMED))

        #
        # Accelerators and peripherals
        #

        # iDMA controller: decodes the windows of the two channels
        idma_mm_ctrl = iDMA_mm_ctrl(self, f'tile-{tid}-idma-ctrl-mm')

        # iDMA channels (idma_axi_obi_transfer_ch): L2 to L1 and L1 to L2
        idma_axi2obi = AxiObiDmaV3(self, f'tile-{tid}-idma-axi2obi', config=AxiObiDmaV3Config(
            axi_to_obi=True, **self.idma_config()))
        idma_obi2axi = AxiObiDmaV3(self, f'tile-{tid}-idma-obi2axi', config=AxiObiDmaV3Config(
            axi_to_obi=False, **self.idma_config()))

        # Redmule
        redmule = LightRedmule(self, f'tile-{tid}-redmule',
                                    tcdm_bank_width     = MagiaArch.BYTES_PER_WORD,
                                    tcdm_bank_number    = 8, # here we set 8 since tcdm_bank_width x tcdm_bank_number --> 8 x 4 = 32bytes, i.e., 256 bits. Please do not consider tcdm_bank_width and tcdm_bank_number as the boundaries of the TCDM, but rather the size of the port towards it.
                                    elem_size           = 2, # max number of bytes per element --> if FP16 then elem_size=2. This is the max number to accomodate any supported format which for now are 8bits and 16bits data types
                                    ce_height           = 8,
                                    ce_width            = 8,
                                    ce_pipe             = 1,
                                    queue_depth         = 1)

        # Fsync mm controller
        fsync_mm_ctrl = FSync_mm_ctrl(self, f'tile-{tid}-fs-ctrl-mm')

        # Event Unit
        self.add_properties(self.ev_unit_config())
        event_unit = Event_unit(self, f'tile-{tid}-event-unit', self.get_property('event_unit/config'))

        # UART
        stdout = StdoutV2(self, f'tile-{tid}-stdout', max_cluster=tree.nb_clusters,
            max_core_per_cluster=1, user_set_core_id=0, user_set_cluster_id=tid)

        #
        # Input ports of the x-bars
        #

        OBI_IN_LOADER       = 0
        OBI_IN_CV32_DATA    = 1
        OBI_IN_AXI          = 2
        OBI_IN_SPATZ_DATA   = 3
        OBI_IN_SPATZ_REFILL = 4
        OBI_IN_PULP_DATA    = 5     # one per PULP core

        AXI_IN_CV32_REFILL  = 0
        AXI_IN_OBI          = 1
        AXI_IN_NOC          = 2
        AXI_IN_PULP_REFILL  = 3

        WIDE_IN_IDMA_AXI2OBI = 0
        WIDE_IN_IDMA_OBI2AXI = 1

        EU_IN_CV32          = 0
        EU_IN_OBI           = 1

        #
        # Bindings
        #

        # Loader -> obi interconnect
        self.__o_LOADER(obi_xbar.i_INPUT(OBI_IN_LOADER))

        if MagiaArch.SPATZ_ENABLE:
            # Snitch spatz core data -> obi interconnect
            snitch_spatz.o_DATA(self.cut('obi-sbr-cut-spatz', obi_xbar.i_INPUT(OBI_IN_SPATZ_DATA), narrow))

            # Snitch spatz core -> snitch spatz icache
            snitch_spatz.o_FETCH(snitch_spatz_i_cache.i_INPUT(0))
            snitch_spatz.o_FLUSH_CACHE(snitch_spatz_i_cache.i_FLUSH())
            snitch_spatz_i_cache.o_FLUSH_ACK(snitch_spatz.i_FLUSH_CACHE_ACK())

            # Snitch spatz icache -> obi interconnect
            snitch_spatz_i_cache.o_REFILL(obi_xbar.i_INPUT(OBI_IN_SPATZ_REFILL))

            # Snitch spatz TCDM
            for port in range(0, MagiaTileTcdm.NB_SPATZ_VLSU):
                snitch_spatz.o_VLSU(port, l1_tcdm.i_SNITCH_SPATZ(port))

            # Snitch spatz core complex registers
            cluster_regs.o_SPATZ_CLK_EN(snitch_spatz.i_FETCHEN())
            cluster_regs.o_SPATZ_START(snitch_spatz.i_IRQ(11))

        if MagiaArch.PULP_ENABLE:
            for pulp_id in range(0,tree.nb_pulp_cores):
                # PULP core data -> obi interconnect
                pulp_cores[pulp_id].o_DATA(obi_xbar.i_INPUT(OBI_IN_PULP_DATA + pulp_id))

                # PULP core -> cluster icache
                pulp_cores[pulp_id].o_FETCH(pulp_i_cache.i_INPUT(pulp_id))
                pulp_cores[pulp_id].o_FLUSH_CACHE(pulp_i_cache.i_FLUSH())
                pulp_i_cache.o_FLUSH_ACK(pulp_cores[pulp_id].i_FLUSH_CACHE_ACK())

                # PULP core complex registers. The start interrupt reaches the
                # core on its machine external interrupt line and stays raised
                # until the core acknowledges it, as in the RTL.
                cluster_regs.o_PULP_ENTRY(pulp_cores[pulp_id].i_ENTRY())
                cluster_regs.o_PULP_CLK_EN(pulp_cores[pulp_id].i_FETCHEN())
                cluster_regs.o_PULP_START(pulp_id, pulp_cores[pulp_id].i_IRQ(11))
                self.bind(pulp_cores[pulp_id], 'irq_ack', cluster_regs, f'pulp_irq_ack_{pulp_id}')

            # Cluster icache -> tile interconnect
            pulp_i_cache.o_REFILL(self.cut('axi-slv-cut-pulp-icache', tile_xbar.i_INPUT(AXI_IN_PULP_REFILL), narrow))

        # Control core data -> event unit direct link / obi interconnect
        core_cv32.o_DATA(cv32_data_demux.i_INPUT())
        cv32_data_demux.o_MAP(self.cut('eu-direct-cut', event_unit_ico.i_INPUT(EU_IN_CV32),
            narrow), RouterMapping(
            base=MagiaArch.EVENT_UNIT_ADDR_START,
            size=MagiaArch.EVENT_UNIT_SIZE, remove_base=True),
            name='event-unit-direct')
        cv32_data_demux.o_MAP_DEFAULT(obi_xbar.i_INPUT(OBI_IN_CV32_DATA), name='obi')

        # Control core -> icache
        core_cv32.o_FETCH(cv32_i_cache.i_INPUT(0))
        core_cv32.o_FLUSH_CACHE(cv32_i_cache.i_FLUSH())
        cv32_i_cache.o_FLUSH_ACK(core_cv32.i_FLUSH_CACHE_ACK())

        # Icache -> tile interconnect
        cv32_i_cache.o_REFILL(self.cut('axi-slv-cut-cv32-icache', tile_xbar.i_INPUT(AXI_IN_CV32_REFILL), narrow))

        # Control core enable ports -> matching composite ports
        self.__o_ENTRY(core_cv32.i_ENTRY())
        self.__o_FETCHEN(core_cv32.i_FETCHEN())

        # Obi xbar -> RedMule
        obi_xbar.o_MAP(self.cut(f'obi-mgr-cut-redmule', redmule.i_INPUT_V2(), narrow), RouterMapping(
            base=MagiaArch.REDMULE_CTRL_ADDR_START,
            size=MagiaArch.REDMULE_CTRL_SIZE, remove_base=True),
            name=f'redmule-mm-{tid}-mem')

        # Obi xbar -> iDMA mmapped controller (idma_obi_ctrl_decoder.sv answers
        # with a combinational rvalid)
        obi_xbar.o_MAP(self.reg_cut(f'obi-mgr-cut-idma', idma_mm_ctrl.i_INPUT()), RouterMapping(
            base=MagiaArch.IDMA_CTRL_ADDR_START,
            size=MagiaArch.IDMA_CTRL_SIZE, remove_base=True),
            name=f'iDMA-ctrl-mm-{tid}-mem')

        # Obi xbar -> fsync mmapped controller (combinational rvalid as well:
        # an access takes the two cycles of the cut on the RTL)
        obi_xbar.o_MAP(self.reg_cut(f'obi-mgr-cut-fsync', fsync_mm_ctrl.i_INPUT()), RouterMapping(
            base=MagiaArch.FSYNC_CTRL_ADDR_START,
            size=MagiaArch.FSYNC_CTRL_SIZE, remove_base=True),
            name=f'fs-ctrl-mm-{tid}-mem')

        # Obi xbar -> event unit
        obi_xbar.o_MAP(self.cut(f'obi-mgr-cut-event-unit', event_unit_ico.i_INPUT(EU_IN_OBI), narrow), RouterMapping(
            base=MagiaArch.EVENT_UNIT_ADDR_START,
            size=MagiaArch.EVENT_UNIT_SIZE, remove_base=True),
            name='event-unit')
        event_unit_ico.o_MAP_DEFAULT(event_unit.i_INPUT(), name='event-unit')

        # Obi xbar -> local stack
        obi_xbar.o_MAP(self.cut(f'obi-mgr-cut-stack', l1_tcdm.i_INPUT(MagiaTileTcdm.NARROW_OBI_STACK), narrow), RouterMapping(
            base=MagiaArch.STACK_ADDR_START,
            size=MagiaArch.STACK_SIZE, remove_base=False),
            name="local-stack")

        if MagiaArch.SPATZ_ENABLE:
            # Obi xbar -> snitch spatz bootrom
            obi_xbar.o_MAP(self.cut(f'obi-mgr-cut-spatz-bootrom', snitch_spatz_rom.i_INPUT(), narrow), RouterMapping(
                base=MagiaArch.SPATZ_BOOTROM_ADDR,
                size=MagiaArch.SPATZ_BOOTROM_SIZE, remove_base=True),
                name="snitch-spatz-bootrom")

        if MagiaArch.SPATZ_ENABLE or MagiaArch.PULP_ENABLE:
            # Obi xbar -> cluster registers
            obi_xbar.o_MAP(self.cut(f'obi-mgr-cut-cluster-regs', cluster_regs.i_INPUT(), narrow), RouterMapping(
                base=MagiaArch.CLUSTER_CTRL_START,
                size=MagiaArch.CLUSTER_CTRL_SIZE, remove_base=True),
                name="cluster-regs")

        # Obi xbar -> local L1
        obi_xbar.o_MAP(self.cut(f'obi-mgr-cut-l1', l1_tcdm.i_INPUT(MagiaTileTcdm.NARROW_OBI_L1), narrow), RouterMapping(
            base=MagiaArch.L1_ADDR_START+(tid*MagiaArch.L1_TILE_OFFSET),
            size=MagiaArch.L1_SIZE, remove_base=False,
            remove_offset=(tid*MagiaArch.L1_TILE_OFFSET)),
            name="obi-to-l1-mem-local")

        # Tile xbar -> obi xbar, for the local L1
        tile_xbar.o_MAP(self.cut('axi-mst-cut-obi', self.cut('obi-sbr-cut-ext',
            obi_xbar.i_INPUT(OBI_IN_AXI), narrow), narrow), RouterMapping(
            base=MagiaArch.L1_ADDR_START+(tid*MagiaArch.L1_TILE_OFFSET),
            size=MagiaArch.L1_SIZE, remove_base=False),
            name="axi-to-obi-l1-mem")

        # Obi xbar -> kill module
        obi_xbar.o_MAP(self.cut(f'obi-mgr-cut-kill', self.__i_KILLER_OUTPUT(), narrow), RouterMapping(
            base=MagiaArch.TEST_END_ADDR_START,
            size=MagiaArch.TEST_END_SIZE, remove_base=False),
            name="Kill-sim-mem")

        # Obi xbar -> uart
        obi_xbar.o_MAP(self.cut(f'obi-mgr-cut-stdout', stdout.i_INPUT(), narrow), RouterMapping(
            base=MagiaArch.STDOUT_ADDR_START,
            size=MagiaArch.STDOUT_SIZE, remove_base=False),
            name="local-uart-mem")

        # Everything that is not local goes to the tile xbar: remote tiles' L1
        # and reserved memory, plus the off-tile L2
        obi_xbar.o_MAP_DEFAULT(self.cut('obi-mgr-cut-l2', self.cut('axi-slv-cut-core-data',
            tile_xbar.i_INPUT(AXI_IN_OBI), narrow), narrow), name="obi2axi-off-tile")

        # Same on the AXI side: everything but the local L1 leaves the tile on
        # the narrow NoC channel
        tile_xbar.o_MAP_DEFAULT(self.cut('axi-mst-cut-noc', self.__i_NARROW_OUTPUT(), narrow),
            name="axi-to-off-tile")

        # NoC narrow channel -> tile xbar
        self.__o_NARROW_INPUT(self.cut('axi-slv-cut-noc', tile_xbar.i_INPUT(AXI_IN_NOC), narrow))

        # NoC wide channel -> local L1
        self.__o_WIDE_INPUT(l1_tcdm.i_WIDE_INPUT(MagiaTileTcdm.WIDE_NOC))

        # iDMA channels: registers, AXI side towards the NoC wide channel, OBI
        # side towards the TCDM
        idma_mm_ctrl.o_AXI2OBI(idma_axi2obi.i_INPUT())
        idma_mm_ctrl.o_OBI2AXI(idma_obi2axi.i_INPUT())
        idma_axi2obi.o_AXI_READ(wide_xbar.i_INPUT(WIDE_IN_IDMA_AXI2OBI))
        idma_obi2axi.o_AXI_WRITE(wide_xbar.i_INPUT(WIDE_IN_IDMA_OBI2AXI))
        idma_axi2obi.o_OBI_WRITE(0, l1_tcdm.i_WIDE_INPUT(MagiaTileTcdm.WIDE_IDMA_AXI2OBI))
        idma_obi2axi.o_OBI_READ(0, l1_tcdm.i_WIDE_INPUT(MagiaTileTcdm.WIDE_IDMA_OBI2AXI))
        wide_xbar.o_MAP_DEFAULT(self.__i_WIDE_OUTPUT(), name="wide-off-tile")

        # Redmule -> local L1
        redmule.o_TCDM(l1_tcdm.i_WIDE_INPUT(MagiaTileTcdm.WIDE_REDMULE))

        # Event unit. Its request drives the machine external interrupt of the
        # control core and the core acknowledges it (magia_tile.sv
        # core_irq_vec, irq_ack_o).
        event_unit.o_IRQ_LINE(0, core_cv32.i_IRQ(11))
        self.bind(core_cv32, 'irq_ack', event_unit, 'irq_ack_0')
        idma_axi2obi.o_IRQ(event_unit.i_EVENT(0, 2))
        idma_obi2axi.o_IRQ(event_unit.i_EVENT(0, 3))
        if MagiaArch.SPATZ_ENABLE:
            self.bind(cluster_regs, 'spatz_done_irq', event_unit, 'in_event_8_pe_0')
        if MagiaArch.PULP_ENABLE:
            self.bind(cluster_regs, 'pulp_done_irq', event_unit, 'in_event_12_pe_0')
        redmule.o_IRQ(event_unit.i_EVENT(0, 10))
        self.bind(fsync_mm_ctrl, 'fsync_done_irq', event_unit, 'in_event_24_pe_0')

        # Fractal sync ports
        fsync_mm_ctrl.o_XIF_2_FRACTAL_EAST_WEST(self.__o_SLAVE_EAST_WEST_FRACTAL())
        self.__i_SLAVE_EAST_WEST_FRACTAL(fsync_mm_ctrl.i_FRACTAL_2_XIF_EAST_WEST())

        fsync_mm_ctrl.o_XIF_2_FRACTAL_NORD_SUD(self.__o_SLAVE_NORD_SUD_FRACTAL())
        self.__i_SLAVE_NORD_SUD_FRACTAL(fsync_mm_ctrl.i_FRACTAL_2_XIF_NORD_SUD())

        fsync_mm_ctrl.o_XIF_2_NEIGHBOUR_FRACTAL_EAST_WEST(self.__o_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL())
        self.__i_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(fsync_mm_ctrl.i_NEIGHBOUR_FRACTAL_2_XIF_EAST_WEST())

        fsync_mm_ctrl.o_XIF_2_NEIGHBOUR_FRACTAL_NORD_SUD(self.__o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL())
        self.__i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(fsync_mm_ctrl.i_NEIGHBOUR_FRACTAL_2_XIF_NORD_SUD())

        # Enable debug
        gdbserver.gdbserver.Gdbserver(self, 'gdbserver')

    def cut(self, name: str, itf: gvsoc.systree.SlaveItf, signature) -> gvsoc.systree.SlaveItf:
        """Register slice in front of ``itf``, for a cut of the RTL (axi_cut,
        obi_cut, the cut of the event-unit direct link): one cycle in each
        direction, one request or response per channel and per cycle. Returns
        its input.

        The slice must be on the beat plane (``signature`` an IoV2Beat): it
        acknowledges writes with io_v2_write_ack(), which rewrites a request
        owned by its master in place (data pointer cleared). A core LSU reuses
        its request objects, so it has to reach the slice through the
        framework's single-request-to-beat adapter, which sends pool beats."""
        cut = IoV2SharedClockBridge(self, f'{self.tid_name}-{name}', signature=signature)
        cut.o_OUTPUT(itf)
        return cut.i_INPUT()

    def reg_cut(self, name: str, itf: gvsoc.systree.SlaveItf,
            resp_latency: int=0) -> gvsoc.systree.SlaveItf:
        """obi_cut of the RTL in front of the register slave ``itf``, for a
        master port of the OBI crossbar. Unlike cut() followed by the
        framework beat-to-single-req adapter, a slave answering inline costs
        the two cycles of the cut only (see ObiCut). Returns its input."""
        cut = ObiCut(self, f'{self.tid_name}-{name}', width=MagiaArch.BYTES_PER_WORD,
            resp_latency=resp_latency)
        cut.o_OUTPUT(itf)
        return cut.i_INPUT()

    @staticmethod
    def idma_config() -> dict:
        """Parameters shared by the two iDMA channels (magia_tile_pkg.sv): a
        wide data path, one OBI port of the same width towards the TCDM, and
        the idma_reg32_3d register file of iDMA v0.6.4."""
        return dict(
            axi_width=MagiaArch.TILE_WIDE_WIDTH,
            num_ax_in_flight=MagiaDSE.TILE_IDMA_NUM_AX_IN_FLIGHT,
            buffer_depth=MagiaDSE.TILE_IDMA_BUFFER_DEPTH,
            req_fifo_depth=MagiaDSE.TILE_IDMA_JOB_FIFO_DEPTH,
            nb_dims=3,
            multireg_count=16,
            obi_port_width=MagiaArch.TILE_WIDE_WIDTH,
            obi_ports_per_access=1)

    # east west port to fractalsync
    def __o_SLAVE_EAST_WEST_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_east_west_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_EAST_WEST_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_east_west_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_EAST_WEST_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('east_west_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_EAST_WEST_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'east_west_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')

    # nord sud to fractalsync
    def __o_SLAVE_NORD_SUD_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_nord_sud_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_NORD_SUD_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_nord_sud_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_NORD_SUD_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('nord_sud_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_NORD_SUD_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'nord_sud_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')
    
    # east west port to neighbour fractalsync
    def __o_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_east_west_neighbour_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_east_west_neighbour_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('east_west_neighbour_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'east_west_neighbour_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')
    
    # nord sud to neighbour fractalsync
    def __o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_nord_sud_neighbour_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_nord_sud_neighbour_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('nord_sud_neighbour_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'nord_sud_neighbour_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')


    # Narrow NoC channel. It is beat-native at the narrow width, so the
    # composite ports carry the beat signature.
    def o_NARROW_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_output', itf, signature=IoV2Beat(MagiaArch.BYTES_PER_WORD))

    def __i_NARROW_OUTPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_output',
            signature=IoV2Beat(MagiaArch.BYTES_PER_WORD))

    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input',
            signature=IoV2Beat(MagiaArch.BYTES_PER_WORD))

    def __o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature=IoV2Beat(MagiaArch.BYTES_PER_WORD),
            composite_bind=True)

    # Wide NoC channel
    def o_WIDE_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_output', itf, signature=IoV2Beat(MagiaArch.TILE_WIDE_WIDTH))

    def __i_WIDE_OUTPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_output',
            signature=IoV2Beat(MagiaArch.TILE_WIDE_WIDTH))

    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input',
            signature=IoV2Beat(MagiaArch.TILE_WIDE_WIDTH))

    def __o_WIDE_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_input', itf, signature=IoV2Beat(MagiaArch.TILE_WIDE_WIDTH),
            composite_bind=True)

    # Input port for the loader. The loader writes chunks of up to 64 KiB and
    # declares no width, while the OBI x-bar behind this port streams word-wide
    # beats and rejects a wider write beat. Declaring the word width here makes
    # the framework insert the width adapter that splits the chunks into
    # word-aligned accesses.
    def i_LOADER(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'loader',
            signature=IoV2SingleReq(width=MagiaArch.BYTES_PER_WORD))

    def __o_LOADER(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('loader', itf,
            signature=IoV2SingleReq(width=MagiaArch.BYTES_PER_WORD),
            composite_bind=True)

    def i_FETCHEN(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'fetchen', signature='wire<bool>')

    def __o_FETCHEN(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('fetchen', itf, signature='wire<bool>', composite_bind=True)

    def i_ENTRY(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'entry', signature='wire<uint64_t>')

    def __o_ENTRY(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('entry', itf, signature='wire<uint64_t>', composite_bind=True)

    # Killer port
    def o_KILLER_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('killer_output', itf, signature=IoV2SingleReq())

    def __i_KILLER_OUTPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'killer_output', signature=IoV2SingleReq())
