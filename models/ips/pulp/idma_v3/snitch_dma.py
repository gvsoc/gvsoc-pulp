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

"""Snitch xdma-driven iDMA (v3).

This module provides the :class:`SnitchDmaV3` generator: the xdma offload
front-end (dmsrc / dmdst / dmstr / dmrep / dmcpy / dmstat instructions of a
Snitch core) in front of the ND mid-end and an AXI-to-AXI back-end. The
defaults describe the Spatz cluster DMA. Not yet wired into the Spatz
cluster, which keeps :class:`ips.pulp.idma_v2.snitch_dma.SnitchDmaV2`.
"""

from typing_extensions import override
import gvsoc.systree
from gvsoc.gui import Signal, DisplayPulse, DisplayLogicBox
from gvsoc.signature import IoV2Beat
from ips.pulp.idma_v3.snitch_dma_config import SnitchDmaV3Config
from ips.pulp.idma_v3.reg_dma import IDMA_V3_SOURCES


class SnitchDmaV3(gvsoc.systree.Component):
    """Snitch xdma-driven iDMA (v3).

    Ports
    -----

    ``i_OFFLOAD()``: the core's offload wire (synchronous; dmcpy is granted or
    refused inside the call, the grant of a refused one comes back through
    ``o_OFFLOAD_GRANT`` and the core replays the instruction).
    ``o_AXI_READ`` / ``o_AXI_WRITE``: the AXI masters (IoV2Beat), to bind on
    two distinct interconnect inputs. ``o_IRQ()``: completion pulse.
    ``o_BUSY()``: high while a transfer is queued or in flight.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, config: SnitchDmaV3Config):
        super().__init__(parent, name, config=config)

        self.add_sources(['ips/pulp/idma_v3/snitch_dma.cpp', 'ips/pulp/idma_v3/fe/idma_fe_xdma.cpp']
            + IDMA_V3_SOURCES)

        self.cfg = config

    def i_OFFLOAD(self) -> gvsoc.systree.SlaveItf:
        """Offload wire from the core."""
        return gvsoc.systree.SlaveItf(self, 'offload', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT(self, itf: gvsoc.systree.SlaveItf):
        """Binds the grant wire towards the core."""
        self.itf_bind('offload_grant', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_AXI_READ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI read master."""
        self.itf_bind('axi_read', itf, signature=IoV2Beat(self.cfg.axi_width))

    def o_AXI_WRITE(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI write master."""
        self.itf_bind('axi_write', itf, signature=IoV2Beat(self.cfg.axi_width))

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the completion pulse (one per completed transfer)."""
        self.itf_bind('irq', itf, signature='wire<bool>')

    def o_BUSY(self, itf: gvsoc.systree.SlaveItf):
        """Binds the busy wire."""
        self.itf_bind('busy', itf, signature='wire<bool>')

    @override
    def gen_gui(self, parent_signal: Signal):
        dma = Signal(self, parent_signal, name=self.name)
        _ = Signal(self, dma, name='active', path='fe/busy',
            groups='regmap', display=DisplayLogicBox('ACTIVE'))
        _ = Signal(self, dma, name='buffer_fill', path='be/buffer_fill', groups='regmap')
        regs = Signal(self, dma, name='regs')
        _ = Signal(self, regs, name='source', path='fe/src', groups='regmap')
        _ = Signal(self, regs, name='dest', path='fe/dst', groups='regmap')
        _ = Signal(self, regs, name='reps', path='fe/reps', groups='regmap')
