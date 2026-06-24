# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

import gvsoc.systree
from memory.memory_v3 import MemoryV3Config


class Ri5kyAsyncMem(gvsoc.systree.Component):
    """Asynchronous SRAM for the ri5ky testbench.

    A byte-addressable backing store that answers IO_REQ_GRANTED and
    completes each request asynchronously ``config.latency`` cycles later
    (via in.resp()), mirroring hw/ri5ky_gwt/gv_tb/slow_mem.sv (gnt the
    request cycle, rvalid LATENCY cycles afterwards). Unlike the
    synchronous memory_v3, this engages p.elw's clock-gated park/wake
    path — the behaviour of a real event unit, which always responds
    asynchronously.

    Reuses :class:`memory.memory_v3.MemoryV3Config` (only ``size`` and
    ``latency`` are consumed).
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str,
                 config: MemoryV3Config):
        super().__init__(parent, name, config=config)
        self.add_sources(['pulp/ri5ky/ri5ky_async_mem.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        # Generic 'io_v2' signature (not IoV2Sync): the master must use its
        # async resp/retry path, which is what parks the core on p.elw.
        return gvsoc.systree.SlaveItf(self, 'input', signature='io_v2')
