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

"""io_v2 Snitch-offload iDMA generator.

This module provides the :class:`SnitchDmaV2` Python generator for the
v2 iDMA's Snitch-style offload front-end
(``ips/pulp/idma_v2/snitch_dma.cpp``). Same back-end stack as
:class:`ips.pulp.idma_v2.reg_dma.RegDmaV2` (2D middle-end, AXI
beat-streaming back-end via the shared
:class:`utils.io_v2_beat_adapter.BeatResponseAdapter`); only the
programming interface differs.
"""

import gvsoc.systree


class SnitchDmaV2(gvsoc.systree.Component):
    """Snitch-offload iDMA on the io_v2 protocol.

    Overview
    ~~~~~~~~

    Programmed via Snitch's accelerator-offload interface rather than
    a memory-mapped register file. Custom instructions (e.g.
    ``dmstr``, ``dmcpyi``, ``dmstati``) arrive on the ``offload``
    slave port carrying the source / destination / length operands;
    the iDMA acknowledges acceptance on ``offload_grant`` and queues
    the descriptor. Everything downstream of the front-end (2D
    middle-end, AXI beat-streaming back-end) is identical to
    :class:`RegDmaV2` — only the programming interface differs.

    The AXI back-end issues one read req per logical burst (size =
    total burst bytes) and lets
    :class:`utils.io_v2_beat_adapter.BeatResponseAdapter` normalise
    the response into one callback per ``axi_width``-sized beat,
    regardless of whether the slave answers sync ``IO_REQ_DONE``,
    async big-packet, or native beat ``resp()``. Any TCDM access goes
    out on the same AXI master pair and must loop back through the
    surrounding interconnect, matching the way the iDMA is wired in
    RTL.

    Ports
    ~~~~~

    - **offload** (slave, ``wire<IssOffloadInsn<uint32_t>*>``) —
      Snitch's offload-instruction channel. The Snitch core posts a
      pointer to a decoded offload-insn struct here; the iDMA reads
      out the operands and decides whether to accept.
    - **offload_grant** (master, ``wire<IssOffloadInsnGrant<uint32_t>*>``)
      — acknowledgement back to the Snitch core: tells it whether
      the instruction was accepted and what return value (if any) to
      write back to the issuing register.
    - **axi_read** / **axi_write** (master, ``io_v2``) — system-side
      reads and writes. Each is its own master because a v2
      ``IoSlave`` can only be bound to one master; use
      :meth:`o_AXI` only against sync-only routers.

    Parameters
    ~~~~~~~~~~

    All parameters are constructor kwargs (no ``Config`` dataclass);
    they are serialised into the JSON config tree and read by the
    C++ model at reset.

    ``transfer_queue_size``
        Maximum number of in-flight transfer descriptors queued in
        the front-end. Default 8.
    ``burst_queue_size``
        Maximum number of in-flight bursts owned by each back-end's
        slot pool. Default 8.
    ``burst_size``
        Optional cap on a logical burst's size (bytes). ``0`` means
        no cap; the back-end uses the AXI page size (4 KiB) as the
        natural upper bound. Default 0.
    ``axi_width``
        Width of the AXI bus in bytes. Used as the beat size on the
        AXI back-end. Default 8.

    Example
    ~~~~~~~

    .. code-block:: python

        dma = SnitchDmaV2(self, 'dma',
            transfer_queue_size=8, burst_queue_size=8,
            axi_width=8)
        snitch.o_OFFLOAD(dma.i_OFFLOAD())
        dma.o_OFFLOAD_GRANT(snitch.i_OFFLOAD_GRANT())
        dma.o_AXI(axi_router.i_INPUT())
    """

    # See :class:`RegDmaV2` for the location of the unified iDMA
    # developer-manual page. Adding ``__gvsoc_doc__`` here would
    # generate a duplicate page.

    def __init__(self, parent: gvsoc.systree.Component, name: str,
            transfer_queue_size: int=8,
            burst_queue_size: int=8,
            burst_size: int=0,
            axi_width: int=8):

        super().__init__(parent, name)

        self.add_sources([
            'ips/pulp/idma_v2/snitch_dma.cpp',
            'ips/pulp/idma_v2/fe/idma_fe_xdma.cpp',
            'ips/pulp/idma_v2/me/idma_me_2d.cpp',
            'ips/pulp/idma_v2/be/idma_be.cpp',
            'ips/pulp/idma_v2/be/idma_be_axi.cpp',
            'utils/io_v2_beat_adapter.cpp',
        ])

        self.add_properties({
            "transfer_queue_size": transfer_queue_size,
            "burst_queue_size": burst_queue_size,
            "burst_size" : burst_size,
            "axi_width": axi_width,
        })

    def i_OFFLOAD(self) -> gvsoc.systree.SlaveItf:
        """Returns the Snitch offload-instruction slave port.

        Snitch posts a pointer to a decoded offload-insn struct here.
        """
        return gvsoc.systree.SlaveItf(self, 'offload',
            signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT(self, itf: gvsoc.systree.SlaveItf):
        """Binds the offload-grant wire back to the Snitch core.

        Carries the iDMA's accept/reject decision and the offload
        instruction's return value (e.g. for ``dmstati``).
        """
        self.itf_bind('offload_grant', itf,
            signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_AXI(self, itf: gvsoc.systree.SlaveItf):
        """Convenience binding — wires both ``axi_read`` and ``axi_write``
        onto the same slave.

        Only safe against sync-only routers (``router_v2_bandwidth``,
        ``router_v2_untimed``). For any beat-aware path bind
        ``axi_read`` and ``axi_write`` to separate router inputs via
        :meth:`itf_bind` directly.
        """
        self.itf_bind('axi_read', itf, signature='io_v2')
        self.itf_bind('axi_write', itf, signature='io_v2')
