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

import gvsoc.systree
from pulp.snitch.snitch_icache import SnitchIcacheL0, SnitchIcacheL1
from gvsoc.signature import IoV2SingleReq
from utils.common_cells import And


class MagiaIcache(gvsoc.systree.Component):
    """Two-level instruction cache (snitch_icache): one private L0 per core in
    front of one shared L1, which refills from the tile interconnect.

    The parameters are those of the RTL instance: ``l0_line_count`` lines in
    each fully associative L0, ``l1_line_count`` sets of ``l1_way_count``
    ways in the L1, ``line_width`` bits per line, ``serial_lookup`` and
    ``num_axi_outstanding`` refills in flight in the L1. MAGIA ties
    enable_prefetching_i to 0 for all its instruction caches (magia_tile.sv),
    so the L0 prefetcher is off.

    The flush input flushes every level, and the flush acknowledge is raised
    once all of them acknowledged.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, nb_cores: int,
            l0_line_count: int, l1_line_count: int, l1_way_count: int, line_width: int,
            serial_lookup: bool, num_axi_outstanding: int):

        super().__init__(parent, name)

        l1_cache = SnitchIcacheL1(self, 'l1', line_count=l1_line_count,
            way_count=l1_way_count, line_width=line_width, nr_fetch_ports=nb_cores,
            num_axi_outstanding=num_axi_outstanding, serial_lookup=serial_lookup)

        flush_ack = And(self, 'flush_ack', nb_input=nb_cores+1)

        for i in range(0, nb_cores):
            l0_cache = SnitchIcacheL0(self, f'l0_{i}', line_count=l0_line_count,
                line_width=line_width)
            l0_cache.i_ENABLE_PREFETCHING().tie(False)
            self.__o_INPUT(i, l0_cache.i_INPUT())
            l0_cache.o_REFILL(l1_cache.i_INPUT(i))
            self.__o_FLUSH(l0_cache.i_FLUSH())
            l0_cache.o_FLUSH_ACK(flush_ack.i_INPUT(i))

        l1_cache.o_REFILL(self.__i_REFILL())
        self.__o_FLUSH(l1_cache.i_FLUSH())
        l1_cache.o_FLUSH_ACK(flush_ack.i_INPUT(nb_cores))

        flush_ack.o_OUTPUT(self.__i_FLUSH_ACK())

    def i_INPUT(self, id: int) -> gvsoc.systree.SlaveItf:
        """Fetch input of core ``id``."""
        return gvsoc.systree.SlaveItf(self, f'input_{id}', signature=IoV2SingleReq())

    def __o_INPUT(self, id: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'input_{id}', itf, signature=IoV2SingleReq(), composite_bind=True)

    def o_REFILL(self, itf: gvsoc.systree.SlaveItf):
        """Binds the refill master of the L1."""
        self.itf_bind('refill', itf, signature=IoV2SingleReq())

    def __i_REFILL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'refill', signature=IoV2SingleReq())

    def i_FLUSH(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'flush', signature='wire<bool>')

    def __o_FLUSH(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('flush', itf, signature='wire<bool>', composite_bind=True)

    def o_FLUSH_ACK(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('flush_ack', itf, signature='wire<bool>')

    def __i_FLUSH_ACK(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'flush_ack', signature='wire<bool>')
