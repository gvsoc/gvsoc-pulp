# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

from typing import ClassVar

import gvrun.timing
from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Beat
from gvsoc.gui import Signal, DisplayLogicBox, DisplayPulse, SignalGenFromSignals
from vp.power_config import PowerSourceConfig, consume_power_model
from gvsoc.signature import IoV2SingleReq


# Ext-side implementation flavours. Each maps to a different C++ source.
KIND_BIG_PACKET = 'big_packet'   # whole-burst io_v2 packet (request-latency timing)
KIND_BEAT       = 'beat'         # per-cycle io_v2 beat streaming (cycle timing)
KIND_AUTO       = 'auto'         # resolve from the hierarchical timing level

_SOURCES = {
    KIND_BIG_PACKET: 'ips/pulp/mchan/mchan.cpp',
    KIND_BEAT:       'ips/pulp/mchan/mchan_beat.cpp',
}

# Timing level -> kind. Only 'cycle' wants beat-accurate ext streaming;
# functional and timed both use the big-packet model.
_LEVEL_TO_KIND = {
    gvrun.timing.FUNCTIONAL: KIND_BIG_PACKET,
    gvrun.timing.TIMED:      KIND_BIG_PACKET,
    gvrun.timing.CYCLE:      KIND_BEAT,
}


class MchanPowerConfig(Config):
    """Transfer power tables of the cluster DMA, filled from the YAML file
    named by the ``power_model`` field of :class:`MchanConfig`; untouched
    sources stay inert."""

    _defer_parent_init: ClassVar[bool] = True

    background: PowerSourceConfig = cfg_field(default_factory=PowerSourceConfig, desc=(
        "Background power (dynamic + leakage)"))
    ext2loc: PowerSourceConfig = cfg_field(default_factory=PowerSourceConfig, desc=(
        "Background power while ext->loc transfers are active"))
    loc2ext: PowerSourceConfig = cfg_field(default_factory=PowerSourceConfig, desc=(
        "Background power while loc->ext transfers are active"))
    both: PowerSourceConfig = cfg_field(default_factory=PowerSourceConfig, desc=(
        "Background power while transfers are active in both directions"))


class MchanConfig(Config):
    nb_channels:         int = cfg_field(default=0,   desc="Number of register channels (one per PE + one for the FC)")
    core_queue_depth:    int = cfg_field(default=2,   desc="Per-channel command queue depth")
    global_queue_depth:  int = cfg_field(default=8,   desc="Global read/write command queue depth")
    max_nb_ext_read_req: int = cfg_field(default=8,   desc="Maximum number of in-flight ext read bursts")
    max_nb_ext_write_req: int = cfg_field(default=8,  desc="Maximum number of in-flight ext write bursts")
    max_burst_length:    int = cfg_field(default=256, desc="Maximum ext burst length (bytes)")
    nb_loc_ports:        int = cfg_field(default=4,   desc="Number of word-wide TCDM ports")
    tcdm_addr_width:     int = cfg_field(default=17,  desc="TCDM address width (loc addresses are masked to this)")
    kind:                str = cfg_field(default=KIND_AUTO, desc=(
        "Ext-side flavour: 'big_packet', 'beat' or 'auto' (default). 'auto' picks the "
        "flavour from the hierarchical timing level (functional/timed->big_packet, "
        "cycle->beat); an explicit kind always wins."))
    ext_width:           int = cfg_field(default=8,   desc="Ext beat width in bytes (beat variant)")
    power_model:         str = cfg_field(default="", desc=(
        "Path of a YAML power model file (background/ext2loc/loc2ext/both "
        "sources), resolved from PYTHONPATH. Applied onto the 'power' tables "
        "at generation time. Empty = no power numbers (all sources inert)."))
    power: MchanPowerConfig = cfg_field(default_factory=MchanPowerConfig,
        init=False, desc=(
        "Transfer power tables, filled from 'power_model'."))


class Mchan(Component):
    """PULP cluster DMA (mchan v7, io_v2).

    One register channel per PE (reached through the per-core peripheral
    demux) plus one for the FC. Moves data between the cluster TCDM (word-wide
    beats on dedicated crossbar inputs) and the SoC (big-packet bursts through
    the cluster soc path). Completion pulses the per-core DMA event/irq lines
    of the event unit, or the FC irq line for the FC channel.
    """

    def __init__(self, parent: Component, name: str, config: MchanConfig):
        if config.kind != KIND_AUTO and config.kind not in _SOURCES:
            raise ValueError(
                f"Mchan kind must be one of {list(_SOURCES) + [KIND_AUTO]}, "
                f"got {config.kind!r}")

        self.config = config
        super().__init__(parent, name, config=config)

        self.nb_loc_ports = config.nb_loc_ports

        # Resolve the ext-side flavour: an explicit kind wins, otherwise it
        # comes from the hierarchical timing level (router_v2 pattern).
        if config.kind == KIND_AUTO:
            level = self.get_timing_level(
                supported=[gvrun.timing.FUNCTIONAL, gvrun.timing.TIMED, gvrun.timing.CYCLE])
            config.kind = _LEVEL_TO_KIND[level]
            self.add_property('kind', config.kind)
            self.add_property('timing_level', level)
        self.kind = config.kind

        self.add_sources([_SOURCES[config.kind]])

        self.add_properties({
            'nb_channels': config.nb_channels,
            'core_queue_depth': config.core_queue_depth,
            'global_queue_depth': config.global_queue_depth,
            'max_nb_ext_read_req': config.max_nb_ext_read_req,
            'max_nb_ext_write_req': config.max_nb_ext_write_req,
            'max_burst_length': config.max_burst_length,
            'nb_loc_ports': config.nb_loc_ports,
            'tcdm_addr_width': config.tcdm_addr_width,
            'ext_width': config.ext_width,
        })

    def configure(self):
        # Consumed here rather than in __init__ so the power_model path can
        # also be set after instantiation (e.g. through a target parameter).
        consume_power_model(self.get_config())

    def gen_gui(self, parent_signal: Signal) -> Signal:
        # Back-ported from the legacy mchan_v7 model. Declaring these signals
        # also enables the underlying events (via include_traces); without a
        # gen_gui the DMA shows nothing in the GUI.
        active = Signal(self, parent_signal, name=self.name, path='busy',
            groups='regmap', display=DisplayLogicBox('ACTIVE'))

        # Per-transfer-id activity (the C++ "channel_0".."channel_15" events,
        # driven to 1 while a transfer on that counter is in flight)
        channels = Signal(self, active, name='channels')
        for i in range(0, 16):
            _ = Signal(self, channels, name=f'channel_{i}', path=f'channel_{i}',
                display=DisplayLogicBox('ACTIVE'), groups='regmap')

        # Last command readied on a channel queue
        queue = Signal(self, active, name='queue')
        for field in ['source', 'dest', 'size', 'stride', 'length']:
            _ = Signal(self, queue, name=field, path=field, groups='regmap')

        # Command currently processed on the ext side, per direction
        ext2loc = Signal(self, active, name='ext2loc')
        for field in ['source', 'dest', 'size', 'stride', 'length']:
            _ = Signal(self, ext2loc, name=f'ext2loc_{field}', path=f'ext2loc_{field}',
                groups='regmap')

        loc2ext = Signal(self, active, name='loc2ext')
        for field in ['source', 'dest', 'size', 'stride', 'length']:
            _ = Signal(self, loc2ext, name=f'loc2ext_{field}', path=f'loc2ext_{field}',
                groups='regmap')

        # In-flight ext traffic. The big-packet variant issues one packet per
        # burst (addr/size/write); the beat variant streams per-cycle beats and
        # exposes the burst-boundary flags instead.
        if self.kind == KIND_BEAT:
            # Top row = the request (beat) address (high-Z between beats); the
            # children carry the beat metadata, plus a resp row showing the
            # response address (high-Z between responses).
            ext_beats = Signal(self, active, name='ext_beats', path='ext_beat_addr',
                groups='regmap')
            _ = Signal(self, ext_beats, name='size', path='ext_beat_size', groups='regmap')
            _ = Signal(self, ext_beats, name='is_write', path='ext_beat_write',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, ext_beats, name='is_first', path='ext_beat_first',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, ext_beats, name='is_last', path='ext_beat_last',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, ext_beats, name='burst_id', path='ext_beat_burst_id', groups='regmap')
            _ = Signal(self, ext_beats, name='resp', path='ext_beat_resp_addr', groups='regmap')
        else:
            ext_bursts = Signal(self, active, name='ext_bursts')
            _ = Signal(self, ext_bursts, name='addr', path='ext_burst_addr', groups='regmap')
            _ = Signal(self, ext_bursts, name='size', path='ext_burst_size', groups='regmap')
            _ = Signal(self, ext_bursts, name='write', path='ext_burst_write',
                display=DisplayPulse(), groups='regmap')

        # In-flight per-port loc beats. The beat variant streams word beats and
        # drives the addresses high-Z when a port is idle: the top loc_beats row
        # shows the aggregate beat address, with one child per port carrying its
        # own address (high-Z when no req) plus size/is_write.
        if self.kind == KIND_BEAT:
            loc_beats = Signal(self, active, name='loc_beats', path='loc_beat_addr',
                groups='regmap')
            for i in range(0, self.nb_loc_ports):
                port = Signal(self, loc_beats, name=f'port[{i}]',
                    path=f'loc_beat_addr[{i}]', groups='regmap')
                _ = Signal(self, port, name='size', path=f'loc_beat_size[{i}]',
                    groups='regmap')
                _ = Signal(self, port, name='is_write', path=f'loc_beat_write[{i}]',
                    display=DisplayPulse(), groups='regmap')
        else:
            loc_bursts = Signal(self, active, name='loc_bursts')
            for i in range(0, self.nb_loc_ports):
                _ = Signal(self, loc_bursts, name=f'addr[{i}]', path=f'loc_burst_addr[{i}]',
                    groups='regmap')
                _ = Signal(self, loc_bursts, name=f'size[{i}]', path=f'loc_burst_size[{i}]',
                    groups='regmap')
                _ = Signal(self, loc_bursts, name=f'write[{i}]', path=f'loc_burst_write[{i}]',
                    display=DisplayPulse(), groups='regmap')

        # End-of-transfer pulses
        _ = Signal(self, active, name='irq_0', path='event', display=DisplayPulse(),
            groups='regmap')
        _ = Signal(self, active, name='irq_1', path='irq', display=DisplayPulse(),
            groups='regmap')

        # Power: single analog row displaying the sum of the DMA's static/dyn
        # power traces (riscv.py pattern)
        import gvsoc.gui
        power_signal = Signal(self, active, name='power', path='power', groups='power',
            required_traces=['static_power_trace', 'dyn_power_trace'],
            display=gvsoc.gui.DisplayAnalog())
        _ = Signal(self, power_signal, name='dynamic', path='dyn_power_trace', groups='power')
        _ = Signal(self, power_signal, name='static', path='static_power_trace', groups='power')

        return active

    # -------- ports --------

    def i_INPUT(self, channel: int) -> SlaveItf:
        """Register channel (CMD/STATUS), one per PE plus one for the FC."""
        return SlaveItf(self, f'in_{channel}', signature=IoV2SingleReq())

    def o_EXT(self, itf: SlaveItf):
        """Burst master towards the SoC (L2).

        The beat variant declares IoV2Beat so the framework auto-inserts an
        IoV2BeatAdapter when the downstream slave uses a non-beat signature;
        the big-packet variant uses the plain io_v2 signature.
        """
        if self.kind == KIND_BEAT:
            self.itf_bind('ext_itf', itf, signature=IoV2Beat(self.config.ext_width))
        else:
            self.itf_bind('ext_itf', itf, signature=IoV2SingleReq())

    def o_LOC(self, port: int, itf: SlaveItf):
        """Word-wide beat master towards a TCDM crossbar input."""
        self.itf_bind(f'loc_itf_{port}', itf, signature=IoV2SingleReq())

    def o_EVENT(self, channel: int, itf: SlaveItf):
        """Per-core end-of-transfer event line (-> event unit DMA0 event)."""
        self.itf_bind(f'event_itf_{channel}', itf, signature='wire<bool>')

    def o_IRQ(self, channel: int, itf: SlaveItf):
        """Per-core end-of-transfer irq line (-> event unit DMA1 event)."""
        self.itf_bind(f'irq_itf_{channel}', itf, signature='wire<bool>')

    def o_EXT_IRQ(self, itf: SlaveItf):
        """FC-channel end-of-transfer irq line (-> FC ITC)."""
        self.itf_bind('ext_irq_itf', itf, signature='wire<bool>')
