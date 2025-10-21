#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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

import gvsoc.systree
from cache.cache import Cache
from interco.router import Router
from utils.common_cells import And
import math

class Hierarchical_cache(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, nb_cores: int=0):

        super(Hierarchical_cache, self).__init__(parent, name)

        #
        # Properties
        #

        line_width = nb_cores * 8
        cache_size = nb_cores * 512
        nb_l0_sets = 1
        nb_l1_sets = nb_cores / 2
        nb_l0_lines = 4
        nb_l1_lines = cache_size / (line_width * nb_l1_sets)
        
        nb_l0_sets_bits = int(math.log2(nb_l0_lines))
        nb_l1_sets_bits = int(math.log2(nb_l1_lines))
        nb_l0_ways_bits = int(math.log2(nb_l0_sets))
        nb_l1_ways_bits = int(math.log2(nb_l1_sets))
        line_size_bits = int(math.log2(line_width))

        #
        # Components
        #

        # L0 caches
        l0_caches = []
        for i in range(0, nb_cores):
            l0_caches.append(Cache(self, 'l0_bank%d' % i, nb_sets_bits=nb_l0_sets_bits, nb_ways_bits=nb_l0_ways_bits, line_size_bits=line_size_bits, refill_latency=0, enabled=True, cache_v2=True))

        # L1 caches
        l1_cache = Cache(self, 'l1', nb_sets_bits=nb_l1_sets_bits, nb_ways_bits=nb_l1_ways_bits, line_size_bits=line_size_bits, refill_latency=0, enabled=True, cache_v2=True)

        # L1 router
        l1_router = Router(self, 'l1_router', bandwidth=line_width)
        l1_router.add_mapping('output')

        # Use an And to gather flush ack from all banks and report a single signal outside
        flush_ack = And(self, 'flush_ack', nb_input=1+nb_cores)

        #
        # Bindings
        #

        # L0 caches
        for i in range(0, nb_cores):
            self.bind(self, 'input_%d' % i, l0_caches[i], 'input')
            self.bind(l0_caches[i], 'refill', l1_router, 'input')
            self.bind(self, 'enable', l0_caches[i], 'enable')
            self.bind(self, 'flush', l0_caches[i], 'flush')

        # L1 cache
        self.bind(l1_cache, 'refill', self, 'refill')
        self.bind(self, 'enable', l1_cache, 'enable')
        self.bind(self, 'flush', l1_cache, 'flush')

        # Router
        self.bind(l1_router, 'output', l1_cache, 'input')

        # Flush ack
        for i in range(0, nb_cores):
            self.bind(l0_caches[i], 'flush_ack', flush_ack, f'input_{i}')
        self.bind(l1_cache, 'flush_ack', flush_ack, f'input_{nb_cores}')

        self.bind(flush_ack, 'output', self, 'flush_ack')