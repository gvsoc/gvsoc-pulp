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

"""Two-stream cluster iDMA (v3).

This module provides the :class:`ClusterDmaV3` generator for the iDMA as
integrated in a PULP cluster: a multi-stream register front-end in front of
two half-duplex streams,

* stream 0: TCDM (OBI) to external memory (AXI),
* stream 1: external memory (AXI) or TCDM (OBI) to TCDM (OBI).

Each stream has its own request FIFO, ND mid-end and back-end (legalizer,
ordering FIFOs, byte-lane buffer); the protocol managers plug into the
back-ends: two AXI beat masters and OBI managers over groups of narrow TCDM
ports driven together, the read group being shared round-robin by both
streams.
"""

from typing_extensions import override
import gvsoc.systree
from gvsoc.gui import Signal, DisplayPulse, DisplayLogicBox
from gvsoc.signature import IoV2Beat, IoV2SingleReq
from ips.pulp.idma_v3.cluster_dma_config import ClusterDmaV3Config
from ips.pulp.idma_v3.reg_dma import IDMA_V3_SOURCES


class ClusterDmaV3(gvsoc.systree.Component):
    """Two-stream cluster iDMA (v3).

    Register map (32-bit accesses, per register port)
    -------------------------------------------------

    ======  ==============  ==============================================
    Offset  Register        Meaning
    ======  ==============  ==============================================
    0x000   CONF            bits 11:10 enable_nd (0 1D, 1 2D, 2 3D), 14:12
                            source protocol, 17:15 destination protocol
                            (AXI 0, OBI 1), bit 0 decouple_rw
    0x004   STATUS_0        busy bits of stream 0
    0x008   STATUS_1        busy bits of stream 1
    0x00C   NEXT_ID_0       read: launch on stream 0, returns the id
    0x010   NEXT_ID_1       read: launch on stream 1, returns the id
    0x014   DONE_ID_0       completion counter of stream 0
    0x018   DONE_ID_1       completion counter of stream 1
    0x0D0   DST_ADDR        destination address
    0x0D8   SRC_ADDR        source address
    0x0E0   LENGTH          bytes of one 1D transfer
    0x0E8   DST_STRIDE_2    0x0F0 SRC_STRIDE_2, 0x0F8 REPS_2
    0x100   DST_STRIDE_3    0x108 SRC_STRIDE_3, 0x110 REPS_3
    ======  ==============  ==============================================

    The stream is selected by the NEXT_ID register that is read; the
    protocols must match the stream (stream 0: OBI to AXI, stream 1: AXI to
    OBI or OBI to OBI), a mismatch is fatal. Identifiers start at 2 and
    DONE_ID counts completions the same way, so a transfer with id ``i`` is
    done once ``(int32) (DONE_ID - i) >= 0``. A launch is back-pressured
    while the stream's request FIFO is full. Every completion pulses all the
    event outputs once, the cycle after the last write is acknowledged. A
    zero-length transfer is rejected at once, ahead of the transfers in
    flight, as in the RTL.

    Ports
    -----

    ``i_INPUT(port)``: register slave of port ``port`` (IoV2SingleReq, the
    launch may be denied). ``o_TCDM_WRITE(i)`` / ``o_TCDM_READ(i)``: the
    narrow TCDM ports (IoV2SingleReq), to bind on the TCDM crossbar.
    ``o_AXI_READ`` / ``o_AXI_WRITE``: the AXI masters (IoV2Beat).
    ``o_EVENT(core)``: completion event of each core; ``o_FC_EVENT``: the
    fabric-controller side event. ``i_ENABLE``: clock gate; when bound, the
    block refuses accesses while it is low. ``busy``: raised while a transfer
    is queued or in flight.
    """

    __gvsoc_doc__ = {
        'title': 'Cluster iDMA (v3)',
        'static_page': '../ips/pulp/idma_v3',
        'tests_dirs': [
            {'dir':       'gvsoc/pulp/tests/idma_v3_cluster',
             'component': 'ips.pulp.idma_v3.cluster_dma'},
        ],
    }

    def __init__(self, parent: gvsoc.systree.Component, name: str, config: ClusterDmaV3Config):
        super().__init__(parent, name, config=config)

        self.add_sources([
            'ips/pulp/idma_v3/cluster_dma.cpp',
            'ips/pulp/idma_v3/fe/idma_fe_reg.cpp',
            'ips/pulp/idma_v3/be/idma_obi_port_group.cpp',
            'ips/pulp/idma_v3/be/idma_obi_read.cpp',
            'ips/pulp/idma_v3/be/idma_obi_write.cpp',
        ] + IDMA_V3_SOURCES)

        self.cfg = config

    def i_INPUT(self, port: int = 0) -> gvsoc.systree.SlaveItf:
        """Register slave of one access port."""
        return gvsoc.systree.SlaveItf(self, f'input_{port}', signature=IoV2SingleReq())

    def i_ENABLE(self) -> gvsoc.systree.SlaveItf:
        """Clock gate input: accesses are refused while it is low."""
        return gvsoc.systree.SlaveItf(self, 'enable', signature='wire<bool>')

    def o_TCDM_WRITE(self, port: int, itf: gvsoc.systree.SlaveItf):
        """Binds one of the TCDM ports the stream 1 writes go through."""
        self.itf_bind(f'tcdm_write_{port}', itf, signature=IoV2SingleReq())

    def o_TCDM_READ(self, port: int, itf: gvsoc.systree.SlaveItf):
        """Binds one of the TCDM ports the reads of both streams share."""
        self.itf_bind(f'tcdm_read_{port}', itf, signature=IoV2SingleReq())

    def o_AXI_READ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI read master (stream 1 sources)."""
        self.itf_bind('axi_read', itf, signature=IoV2Beat(self.cfg.axi_width))

    def o_AXI_WRITE(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI write master (stream 0 destinations)."""
        self.itf_bind('axi_write', itf, signature=IoV2Beat(self.cfg.axi_width))

    def o_EVENT(self, core: int, itf: gvsoc.systree.SlaveItf):
        """Binds the completion event of one core (pulsed on every completion)."""
        self.itf_bind(f'event_{core}', itf, signature='wire<bool>')

    def o_FC_EVENT(self, itf: gvsoc.systree.SlaveItf):
        """Binds the fabric-controller side completion event."""
        self.itf_bind('fc_event', itf, signature='wire<bool>')

    def o_BUSY(self, itf: gvsoc.systree.SlaveItf):
        """Binds the busy wire (high while any transfer is queued or in flight)."""
        self.itf_bind('busy', itf, signature='wire<bool>')

    @override
    def gen_gui(self, parent_signal: Signal):
        dma = Signal(self, parent_signal, name=self.name)
        for stream in range(2):
            active = Signal(self, dma, name=f'stream{stream}', path=f'fe/stream{stream}/busy',
                groups='regmap', display=DisplayLogicBox('ACTIVE'))
            _ = Signal(self, active, name='id', path=f'fe/stream{stream}/id', groups='regmap',
                display=DisplayPulse())
            _ = Signal(self, active, name='buffer_fill', path=f'be{stream}/buffer_fill',
                groups='regmap')
            # Completion: the identifier counted done, DONE_ID and the transfers
            # still queued in front of the stream
            _ = Signal(self, active, name='done', path=f'fe/stream{stream}/done', groups='regmap',
                display=DisplayPulse())
            _ = Signal(self, active, name='done_id', path=f'fe/stream{stream}/done_id',
                groups='regmap')
            _ = Signal(self, active, name='queue', path=f'me{stream}/queue', groups='regmap')

            # Where the transfer is: the line handed to the back-end, and the
            # burst the legalizer comes to on each side
            progress = Signal(self, active, name='progress', path=f'me{stream}/line',
                groups='regmap')
            _ = Signal(self, progress, name='lines_left', path=f'me{stream}/lines_left',
                groups='regmap')
            _ = Signal(self, progress, name='src', path=f'me{stream}/src', groups='regmap')
            _ = Signal(self, progress, name='dst', path=f'me{stream}/dst', groups='regmap')
            _ = Signal(self, progress, name='read_addr', path=f'be{stream}/r_addr',
                groups='regmap')
            _ = Signal(self, progress, name='read_left', path=f'be{stream}/r_left',
                groups='regmap')
            _ = Signal(self, progress, name='write_addr', path=f'be{stream}/w_addr',
                groups='regmap')
            _ = Signal(self, progress, name='write_left', path=f'be{stream}/w_left',
                groups='regmap')

            # What holds the data path of the stream
            _ = Signal(self, active, name='buffer_full', path=f'be{stream}/buffer_full',
                groups='regmap', display=DisplayLogicBox('FULL'))
            _ = Signal(self, active, name='buffer_empty', path=f'be{stream}/buffer_empty',
                groups='regmap', display=DisplayLogicBox('EMPTY'))

        # The buses. Stream 0 writes the AXI and stream 1 reads it; the TCDM
        # read ports are shared by the two streams, the write ports are
        # stream 1's.
        axi_read = Signal(self, dma, name='axi_read', path='axi_read/r_addr', groups='regmap')
        _ = Signal(self, axi_read, name='ar_addr', path='axi_read/ar_addr', groups='regmap')
        _ = Signal(self, axi_read, name='ar_beats', path='axi_read/ar_beats', groups='regmap')
        _ = Signal(self, axi_read, name='ar_wait', path='axi_read/ar_wait', groups='regmap',
            display=DisplayLogicBox('WAIT'))
        _ = Signal(self, axi_read, name='r_last', path='axi_read/r_last', groups='regmap',
            display=DisplayPulse())
        _ = Signal(self, axi_read, name='r_wait', path='axi_read/r_wait', groups='regmap',
            display=DisplayLogicBox('WAIT'))
        bursts = Signal(self, axi_read, name='bursts')
        for slot in range(self.cfg.num_ax_in_flight + 1):
            _ = Signal(self, bursts, name=f'burst_{slot}', path=f'axi_read/burst_{slot}',
                groups='regmap')

        axi_write = Signal(self, dma, name='axi_write', path='axi_write/w_addr', groups='regmap')
        _ = Signal(self, axi_write, name='w_size', path='axi_write/w_size', groups='regmap')
        _ = Signal(self, axi_write, name='w_last', path='axi_write/w_last', groups='regmap',
            display=DisplayPulse())
        _ = Signal(self, axi_write, name='w_wait', path='axi_write/w_wait', groups='regmap',
            display=DisplayLogicBox('WAIT'))
        _ = Signal(self, axi_write, name='b', path='axi_write/b', groups='regmap',
            display=DisplayPulse())
        bursts = Signal(self, axi_write, name='bursts')
        nb_write_slots = (self.cfg.meta_fifo_depth if self.cfg.meta_fifo_depth > 0
            else self.cfg.num_ax_in_flight + 3) + 1
        for slot in range(nb_write_slots):
            _ = Signal(self, bursts, name=f'burst_{slot}', path=f'axi_write/burst_{slot}',
                groups='regmap')

        for name in ['tcdm_read', 'tcdm_write']:
            tcdm = Signal(self, dma, name=name, path=f'{name}/addr', groups='regmap')
            _ = Signal(self, tcdm, name='size', path=f'{name}/size', groups='regmap')
            _ = Signal(self, tcdm, name='grant_wait', path=f'{name}/grant_wait',
                groups='regmap', display=DisplayLogicBox('WAIT'))
            for port in range(self.cfg.obi_ports_per_access):
                _ = Signal(self, tcdm, name=f'port[{port}]', path=f'{name}/port_{port}',
                    groups='regmap')

        for port in range(self.cfg.nb_reg_ports):
            regs = Signal(self, dma, name=f'port{port}')
            _ = Signal(self, regs, name='source', path=f'fe/port{port}/src', groups='regmap')
            _ = Signal(self, regs, name='dest', path=f'fe/port{port}/dst', groups='regmap')
            _ = Signal(self, regs, name='length', path=f'fe/port{port}/length', groups='regmap')
            _ = Signal(self, regs, name='reps', path=f'fe/port{port}/reps_2', groups='regmap')
