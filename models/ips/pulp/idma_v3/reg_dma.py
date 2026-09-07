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

"""Single-stream register-driven iDMA (v3).

This module provides the :class:`RegDmaV3` generator: the v3 register
front-end (one port, one stream) in front of the ND mid-end and a back-end
with one AXI read and one AXI write manager. It is the AXI-to-AXI shape of
the iDMA (Cheshire, voscap, the Spatz cluster DMA once driven by the xdma
front-end) and the vehicle of the standalone tests.
"""

from typing_extensions import override
import gvsoc.systree
from gvsoc.gui import Signal, DisplayPulse, DisplayLogicBox
from gvsoc.signature import IoV2Beat, IoV2SingleReq
from ips.pulp.idma_v3.reg_dma_config import RegDmaV3Config

IDMA_V3_SOURCES = [
    'ips/pulp/idma_v3/me/idma_me_nd.cpp',
    'ips/pulp/idma_v3/be/idma_buffer.cpp',
    'ips/pulp/idma_v3/be/idma_legalizer.cpp',
    'ips/pulp/idma_v3/be/idma_be.cpp',
    'ips/pulp/idma_v3/be/idma_axi_read.cpp',
    'ips/pulp/idma_v3/be/idma_axi_write.cpp',
]


class RegDmaV3(gvsoc.systree.Component):
    """Single-stream register-driven iDMA (v3).

    Register map (32-bit accesses)
    ------------------------------

    ======  ==============  ==============================================
    Offset  Register        Meaning
    ======  ==============  ==============================================
    0x000   CONF            bits 11:10 enable_nd (0 1D, 1 2D, 2 3D), 14:12
                            source protocol, 17:15 destination protocol
                            (AXI 0), bit 0 decouple_rw, bit 1 decouple_aw
    0x004   STATUS          bits 7:0 back-end busy, bit 8 mid-end busy
    0x008   NEXT_ID         read: launch, returns the id
    0x00C   DONE_ID         completion counter
    0x0D0   DST_ADDR        destination address
    0x0D8   SRC_ADDR        source address
    0x0E0   LENGTH          bytes of one 1D transfer
    0x0E8   DST_STRIDE_2    0x0F0 SRC_STRIDE_2, 0x0F8 REPS_2
    0x100   DST_STRIDE_3    0x108 SRC_STRIDE_3, 0x110 REPS_3
    ======  ==============  ==============================================

    Identifiers start at 2 and DONE_ID counts completions the same way, so a
    transfer with id ``i`` is done once ``(int32) (DONE_ID - i) >= 0``. A
    launch is back-pressured while the request FIFO is full. Every completion
    pulses ``irq``.

    Ports
    -----

    ``i_INPUT()``: register slave (IoV2SingleReq, the launch may be denied).
    ``o_AXI_READ`` / ``o_AXI_WRITE``: the AXI masters (IoV2Beat), to bind on
    two distinct interconnect inputs. ``o_IRQ()``: completion pulse.
    ``o_BUSY()``: high while a transfer is queued or in flight.
    """

    __gvsoc_doc__ = {
        'title': 'iDMA (v3)',
        'static_page': '../ips/pulp/idma_v3',
        'tests_dirs': [
            {'dir':       'gvsoc/pulp/tests/idma_v3_axi',
             'component': 'ips.pulp.idma_v3.reg_dma'},
        ],
    }

    def __init__(self, parent: gvsoc.systree.Component, name: str, config: RegDmaV3Config):
        super().__init__(parent, name, config=config)

        self.add_sources(['ips/pulp/idma_v3/reg_dma.cpp', 'ips/pulp/idma_v3/fe/idma_fe_reg.cpp']
            + IDMA_V3_SOURCES)

        self.cfg = config

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        """Register slave."""
        return gvsoc.systree.SlaveItf(self, 'input_0', signature=IoV2SingleReq())

    def o_AXI_READ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI read master."""
        self.itf_bind('axi_read', itf, signature=IoV2Beat(self.cfg.axi_width))

    def o_AXI_WRITE(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI write master."""
        self.itf_bind('axi_write', itf, signature=IoV2Beat(self.cfg.axi_width))

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the completion pulse (one per completed transfer)."""
        self.itf_bind('fc_event', itf, signature='wire<bool>')

    def o_BUSY(self, itf: gvsoc.systree.SlaveItf):
        """Binds the busy wire."""
        self.itf_bind('busy', itf, signature='wire<bool>')

    @override
    def gen_gui(self, parent_signal: Signal):
        dma = Signal(self, parent_signal, name=self.name)
        active = Signal(self, dma, name='active', path='fe/stream0/busy',
            groups='regmap', display=DisplayLogicBox('ACTIVE'))
        _ = Signal(self, active, name='id', path='fe/stream0/id', groups='regmap',
            display=DisplayPulse())
        _ = Signal(self, dma, name='buffer_fill', path='be/buffer_fill', groups='regmap')
        regs = Signal(self, dma, name='regs')
        _ = Signal(self, regs, name='source', path='fe/port0/src', groups='regmap')
        _ = Signal(self, regs, name='dest', path='fe/port0/dst', groups='regmap')
        _ = Signal(self, regs, name='length', path='fe/port0/length', groups='regmap')
        _ = Signal(self, regs, name='reps', path='fe/port0/reps_2', groups='regmap')
