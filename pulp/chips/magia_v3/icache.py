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
from pulp.snitch.snitch_icache import SnitchIcache, SnitchIcacheConfig
from interco.router_v2 import Router, RouterConfig, KIND_UNTIMED
from gvsoc.signature import IoV2SingleReq
from utils.common_cells import And


class MagiaIcache(gvsoc.systree.Component):
    """Two-level instruction cache: one private L0 per core in front of one
    shared L1, which refills from the tile interconnect.

    Both levels are SnitchIcache. The geometry is given in bytes. With
    several cores, the L0 refills reach the L1 through an untimed router, since
    an io_v2 slave binds a single master; with one core the L0 refills the L1
    directly.

    The flush input flushes every level, and the flush acknowledge is raised
    once all of them acknowledged.

    ``l0_refill_latency`` is what an L0 miss costs on top of the L1 access,
    i.e. the cycles the RTL (snitch_icache) takes to send the refill to the L1,
    look the line up there and write it into the L0.

    The RTL L0 is fully associative (give ``l0_ways`` = number of L0 lines)
    and replaces its lines in turn (snitch_icache_l0.sv), hence a
    SnitchIcache with round_robin. The RTL L1 picks the way to replace with
    one eviction LFSR for the whole cache, whether the set has free ways or
    not (snitch_icache_handler.sv): ``l1_rtl_lfsr``.

    The L0 answers asynchronously when an access has a latency
    (async_latency): the iss_v2 prefetcher ignores the latency of an inline
    fetch answer (it only goes to its statistics,
    core/models/cpu/iss_v2/src/prefetch/prefetch_single_line.cpp), so an L0
    miss refilled from the L1 would cost the core nothing.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, nb_cores: int,
            l0_size: int, l0_line_size: int, l0_ways: int,
            l1_size: int, l1_line_size: int, l1_ways: int, l1_refill_latency: int,
            l0_refill_latency: int=0, l1_rtl_lfsr: bool=False):

        super().__init__(parent, name)

        l0_caches = []
        for i in range(0, nb_cores):
            l0_caches.append(SnitchIcache(self, f'l0_{i}', config=SnitchIcacheConfig(
                size=l0_size, line_size=l0_line_size, ways=l0_ways,
                refill_latency=l0_refill_latency, round_robin=True,
                async_latency=True)))

        l1_cache = SnitchIcache(self, 'l1', config=SnitchIcacheConfig(
            size=l1_size, line_size=l1_line_size, ways=l1_ways,
            refill_latency=l1_refill_latency, rtl_lfsr=l1_rtl_lfsr))

        flush_ack = And(self, 'flush_ack', nb_input=nb_cores+1)

        if nb_cores > 1:
            refill_ico = Router(self, 'refill_ico', config=RouterConfig(kind=KIND_UNTIMED))
            refill_ico.o_MAP_DEFAULT(l1_cache.i_INPUT(), name='l1')

        for i in range(0, nb_cores):
            self.__o_INPUT(i, l0_caches[i].i_INPUT())
            if nb_cores > 1:
                l0_caches[i].o_REFILL(refill_ico.i_INPUT(i))
            else:
                l0_caches[i].o_REFILL(l1_cache.i_INPUT())
            self.__o_FLUSH(l0_caches[i].i_FLUSH())
            l0_caches[i].o_FLUSH_ACK(flush_ack.i_INPUT(i))

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
