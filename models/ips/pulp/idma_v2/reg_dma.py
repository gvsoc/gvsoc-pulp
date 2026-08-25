#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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

"""io_v2 register-programmed iDMA generator.

This module provides the :class:`RegDmaV2` Python generator for the v2
iDMA's register front-end (``ips/pulp/idma_v2/reg_dma.cpp``), wiring
the register front-end (``fe/idma_fe_reg.cpp``), the 2D middle-end
(``me/idma_me_2d.cpp``) and the AXI beat-streaming back-end
(``be/idma_be_axi.cpp``) into a single io_v2 component. The AXI
back-end runs every burst through a
:class:`utils.io_v2_beat_adapter.BeatResponseAdapter` so the iDMA sees
a uniform per-beat callback stream regardless of whether the slave
answers with sync ``IO_REQ_DONE``, async big-packet, or native beat
``resp()``.

The iDMA exposes a single AXI master pair to the outside world. Any
access — including ones that target the local TCDM — leaves the
component on the AXI side and must be looped back through the
surrounding interconnect, matching the way the iDMA is integrated in
RTL.
"""

from typing_extensions import override
import gvsoc.systree
from gvsoc.gui import Signal, DisplayPulse, DisplayLogicBox
from gvsoc.signature import IoV2Beat
from ips.pulp.idma_v2.reg_dma_config import RegDmaConfig
from gvsoc.signature import IoV2Sync


class RegDmaV2(gvsoc.systree.Component):
    """Register-programmed iDMA on the io_v2 protocol.

    Overview
    ~~~~~~~~

    Same pipeline shape as the v1 :class:`pulp.idma.reg_dma.RegDma`,
    but every IO port speaks io_v2 (``vp/itf/io_v2.hpp``). The
    component combines three sub-blocks compiled into one shared
    library:

    - **Register front-end** (``fe/idma_fe_reg.cpp``) — drives the
      ``input`` slave port, decoding register writes into 1D/2D
      transfer descriptors and producing the completion ``irq``.
    - **2D middle-end** (``me/idma_me_2d.cpp``) — fans the queued
      descriptors out as a stream of 1D transfers.
    - **AXI back-end** (``be/idma_be_axi.cpp``) — uses
      :class:`utils.io_v2_beat_adapter.BeatResponseAdapter` to
      issue one read req per burst (size = full burst bytes) and
      stream beat-sized writes onto ``axi_read`` / ``axi_write``.

    Both ends of every transfer go through this AXI back-end pair,
    matching the RTL where the iDMA has no TCDM shortcut. Any access
    that needs to land on a TCDM is the surrounding interconnect's
    responsibility (typically via an AXI-to-mem bridge in front of the
    bank-side splitter).

    Ports
    ~~~~~

    - **input** (slave, ``io_v2``) — CPU-side register file. Writes
      enqueue transfer descriptors; reads return DMA status.
    - **axi_read** / **axi_write** (master, ``io_v2``) — system-side
      reads and writes. Each is its own master because a v2
      ``IoSlave`` can only be bound to one master. Bind to two
      separate router inputs (or use the convenience
      :meth:`o_AXI` if a single sync-only router is upstream).
    - **irq** (master, ``wire<bool>``) — pulsed high on transfer
      completion (one pulse per descriptor).

    Configuration fields (:class:`RegDmaConfig`)
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    ``transfer_queue_size``
        Maximum number of in-flight transfer descriptors queued in
        the front-end. Default 8.
    ``burst_queue_size``
        Maximum number of in-flight bursts owned by each back-end's
        slot pool. Default 8.
    ``burst_size``
        Optional cap on a logical burst's size (bytes). ``0`` means
        no cap; the back-end will use the AXI page size (4 KiB) as
        the natural upper bound. Default 0.
    ``axi_width``
        Width of the AXI bus in bytes. Used as the beat size on the
        AXI back-end — a logical burst of size ``S`` is announced
        in a single io_v2 req and the adapter spreads the
        ``ceil(S / axi_width)`` response beats one per cycle.

    Example
    ~~~~~~~

    .. code-block:: python

        dma = RegDmaV2(self, 'dma',
            config=RegDmaConfig(
                transfer_queue_size=8,
                burst_queue_size=8,
                axi_width=8))
        cpu_periph.o_OUT(dma.i_INPUT())
        dma.o_AXI_READ(axi_router.i_INPUT())
        dma.o_AXI_WRITE(axi_router.i_INPUT())
        dma.o_IRQ(itc.i_EVENT(dma_irq))

    Parameters
    ----------
    parent : Component
        Parent component this iDMA is instantiated under.
    name : str
        Local name within ``parent``.
    config : RegDmaConfig
        Full configuration — every tunable lives on this object.
    """

    # Developer-manual doc registration. The v2 iDMA covers three front-
    # ends + one 2D middle-end + two beat-streaming back-ends, so it ships
    # a single hand-written page that documents the full pipeline in one
    # place — ``components/ips/pulp/idma_v2.rst`` in the engine docs. The
    # ``static_page`` field tells component_pages.py to skip its
    # auto-generation for this class and reference the hand-written page
    # from the generated components index instead, so the v2 iDMA still
    # appears in the same ``ips`` group as the other ips.* components.
    # :class:`SnitchDmaV2` and :class:`CheshireDmaV2` deliberately do not
    # declare ``__gvsoc_doc__`` — the unified page already pulls their
    # docstrings in via ``.. autoclass::``.
    __gvsoc_doc__ = {
        'title': 'iDMA (v2)',
        'static_page': '../ips/pulp/idma_v2',
        'tests_dirs': [
            {'dir':       'gvsoc/pulp/tests/idma_v2',
             'component': 'ips.pulp.idma_v2.reg_dma'},
        ],
    }

    def __init__(self, parent: gvsoc.systree.Component, name: str, config: RegDmaConfig,
    ):

        super().__init__(parent, name)

        self.add_sources([
            'ips/pulp/idma_v2/reg_dma.cpp',
            'ips/pulp/idma_v2/fe/idma_fe_reg.cpp',
            'ips/pulp/idma_v2/me/idma_me_2d.cpp',
            'ips/pulp/idma_v2/be/idma_be.cpp',
            'ips/pulp/idma_v2/be/idma_be_axi.cpp',
        ])

        self.add_properties({
            "transfer_queue_size": config.transfer_queue_size,
            "burst_queue_size": config.burst_queue_size,
            "burst_size" : config.burst_size,
            "axi_width": config.axi_width,
        })

        # Remember axi_width on the Python object — the o_AXI_* methods
        # declare beat-mode bindings against this.
        self._axi_width = config.axi_width

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        """Returns the io_v2 register-file slave port.

        CPU writes here program transfer descriptors; CPU reads
        return DMA status registers.
        """
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Sync())

    def o_AXI(self, itf: gvsoc.systree.SlaveItf):
        """Convenience binding — wires both ``axi_read`` and ``axi_write``
        onto the same slave.

        A v2 ``IoSlave`` can only be bound to ONE master, so this
        form silently overwrites the read master's ``resp_meth`` /
        ``retry_meth`` with the write's. Safe only against sync-only
        routers (``router_v2_bandwidth``, ``router_v2_untimed``) that
        never call ``resp()``. For any beat-aware path use
        :meth:`o_AXI_READ` and :meth:`o_AXI_WRITE` on separate
        router inputs.
        """
        self.itf_bind('axi_read', itf, signature=IoV2Beat(self._axi_width))
        self.itf_bind('axi_write', itf, signature=IoV2Beat(self._axi_width))

    def o_AXI_READ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the system-side AXI read master to ``itf``.

        Each logical read burst is issued as exactly one io_v2 req
        of size = total burst bytes. The framework auto-inserts an
        ``IoV2BeatAdapter`` on the path when the bound slave is
        ``IoV2BigPacket`` so the back-end always observes per-beat
        responses (one callback per ``config.axi_width`` bytes).
        """
        self.itf_bind('axi_read', itf, signature=IoV2Beat(self._axi_width))

    def o_AXI_WRITE(self, itf: gvsoc.systree.SlaveItf):
        """Binds the system-side AXI write master to ``itf``.

        Writes are streamed beat-by-beat (one io_v2 req per
        ``config.axi_width`` bytes, ``is_first`` / ``is_last`` /
        ``burst_id`` set per beat).
        """
        self.itf_bind('axi_write', itf, signature=IoV2Beat(self._axi_width))

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the completion interrupt wire to ``itf``.

        One pulse per descriptor completion.
        """
        self.itf_bind('irq', itf, signature='wire<bool>')

    @override
    def gen_gui(self, parent_signal: Signal):
        active = Signal(self, parent_signal, name=self.name, path='fe/busy', groups='regmap',
            display=DisplayLogicBox('ACTIVE'))

        # This shows details about all the transfers which are queued to the DMA
        queue = Signal(self, active, name='queue')
        _ = Signal(self, queue, name='source', path='fe/src', groups='regmap')
        _ = Signal(self, queue, name='dest', path='fe/dst', groups='regmap')
        _ = Signal(self, queue, name='length', path='fe/length', groups='regmap')
        _ = Signal(self, queue, name='src_stride', path='fe/src_stride', groups='regmap')
        _ = Signal(self, queue, name='dst_stride', path='fe/dst_stride', groups='regmap')
        _ = Signal(self, queue, name='reps', path='fe/reps', groups='regmap')
        _ = Signal(self, queue, name='id', path='fe/id', groups='regmap',
            display=DisplayPulse())
