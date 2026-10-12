#
# Copyright (C) 2026 Fondazione Chips-IT
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
# Authors: Lorenzo Zuolo, Fondazione Chips-IT (lorenzo.zuolo@chips.it)
#

"""Single-direction AXI/OBI iDMA (v3).

This module provides the :class:`AxiObiDmaV3` generator: the v3 register
front-end (one port, one stream) in front of the ND mid-end and a back-end
with one AXI and one OBI manager, in one direction. It is the shape of the
iDMA channels of MAGIA (idma_axi_obi_transfer_ch): an AXI to OBI channel
copies into the local memory, an OBI to AXI one copies out of it, and a tile
instantiates one of each.
"""

from typing_extensions import override
import gvsoc.systree
from gvsoc.gui import Signal, DisplayPulse, DisplayLogicBox
from gvsoc.signature import IoV2Beat, IoV2SingleReq
from ips.pulp.idma_v3.axi_obi_dma_config import AxiObiDmaV3Config
from ips.pulp.idma_v3.reg_dma import IDMA_V3_SOURCES


class AxiObiDmaV3(gvsoc.systree.Component):
    """Single-stream, single-direction AXI/OBI iDMA (v3).

    Register map (32-bit accesses)
    ------------------------------

    The idma_reg32_3d register file of iDMA v0.6.4, with the default
    ``multireg_count`` of 16:

    ======  ==============  ==============================================
    Offset  Register        Meaning
    ======  ==============  ==============================================
    0x000   CONF            bit 0 decouple_rw, bit 1 decouple_aw (as the
                            idma_v3 register front-end; idma_reg32_3d has
                            them the other way round), bits 11:10
                            enable_nd (0 1D, 1 2D, 2 3D), 14:12 source
                            protocol, 17:15 destination protocol (AXI 0,
                            OBI 1)
    0x004   STATUS          bits 7:0 back-end busy, bit 8 mid-end busy
    0x044   NEXT_ID         read: launch, returns the id
    0x084   DONE_ID         completion counter
    0x0D0   DST_ADDR        destination address
    0x0D8   SRC_ADDR        source address
    0x0E0   LENGTH          bytes of one 1D transfer
    0x0E8   DST_STRIDE_2    0x0F0 SRC_STRIDE_2, 0x0F8 REPS_2
    0x100   DST_STRIDE_3    0x108 SRC_STRIDE_3, 0x110 REPS_3
    ======  ==============  ==============================================

    The protocols written in CONF must match the direction of the channel (AXI
    to OBI or OBI to AXI), a mismatch is fatal. Identifiers start at 2 and
    DONE_ID counts completions the same way, so a transfer with id ``i`` is
    done once ``(int32) (DONE_ID - i) >= 0``. A launch is back-pressured while
    the request FIFO is full. Every completion pulses ``irq``.

    Ports
    -----

    ``i_INPUT()``: register slave (IoV2SingleReq, the launch may be denied).
    ``o_AXI_READ`` / ``o_AXI_WRITE``: the AXI master of the direction
    (IoV2Beat). ``o_OBI_READ(i)`` / ``o_OBI_WRITE(i)``: the OBI ports of the
    direction (IoV2SingleReq). ``o_IRQ()``: completion pulse. ``o_BUSY()``:
    high while a transfer is queued or in flight.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, config: AxiObiDmaV3Config):
        super().__init__(parent, name, config=config)

        self.add_sources([
            'ips/pulp/idma_v3/axi_obi_dma.cpp',
            'ips/pulp/idma_v3/fe/idma_fe_reg.cpp',
            'ips/pulp/idma_v3/be/idma_obi_port_group.cpp',
            'ips/pulp/idma_v3/be/idma_obi_read.cpp',
            'ips/pulp/idma_v3/be/idma_obi_write.cpp',
        ] + IDMA_V3_SOURCES)

        self.cfg = config

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        """Register slave."""
        return gvsoc.systree.SlaveItf(self, 'input_0', signature=IoV2SingleReq())

    def o_AXI_READ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI read master (AXI to OBI channel)."""
        assert self.cfg.axi_to_obi, 'The OBI to AXI channel has no AXI read master'
        self.itf_bind('axi_read', itf, signature=IoV2Beat(self.cfg.axi_width))

    def o_AXI_WRITE(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI write master (OBI to AXI channel)."""
        assert not self.cfg.axi_to_obi, 'The AXI to OBI channel has no AXI write master'
        self.itf_bind('axi_write', itf, signature=IoV2Beat(self.cfg.axi_width))

    def o_OBI_READ(self, port: int, itf: gvsoc.systree.SlaveItf):
        """Binds one of the OBI read ports (OBI to AXI channel)."""
        assert not self.cfg.axi_to_obi, 'The AXI to OBI channel has no OBI read port'
        self.itf_bind(f'obi_read_{port}', itf, signature=IoV2SingleReq())

    def o_OBI_WRITE(self, port: int, itf: gvsoc.systree.SlaveItf):
        """Binds one of the OBI write ports (AXI to OBI channel)."""
        assert self.cfg.axi_to_obi, 'The OBI to AXI channel has no OBI write port'
        self.itf_bind(f'obi_write_{port}', itf, signature=IoV2SingleReq())

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the completion pulse (one per completed transfer)."""
        self.itf_bind('fc_event', itf, signature='wire<bool>')

    def o_BUSY(self, itf: gvsoc.systree.SlaveItf):
        """Binds the busy wire."""
        self.itf_bind('busy', itf, signature='wire<bool>')

    @override
    def gen_gui(self, parent_signal: Signal):
        dma = Signal(self, parent_signal, name=self.name)
        _ = Signal(self, dma, name='irq', path='fe/irq', groups='regmap',
            display=DisplayPulse())
        active = Signal(self, dma, name='active', path='fe/stream0/busy',
            groups='regmap', display=DisplayLogicBox('ACTIVE'))
        _ = Signal(self, active, name='id', path='fe/stream0/id', groups='regmap',
            display=DisplayPulse())
        _ = Signal(self, active, name='done', path='fe/stream0/done', groups='regmap',
            display=DisplayPulse())
        _ = Signal(self, active, name='done_id', path='fe/stream0/done_id', groups='regmap')
        _ = Signal(self, active, name='buffer_fill', path='be/buffer_fill', groups='regmap')
        regs = Signal(self, dma, name='regs')
        _ = Signal(self, regs, name='source', path='fe/port0/src', groups='regmap')
        _ = Signal(self, regs, name='dest', path='fe/port0/dst', groups='regmap')
        _ = Signal(self, regs, name='length', path='fe/port0/length', groups='regmap')
        _ = Signal(self, regs, name='reps', path='fe/port0/reps_2', groups='regmap')
