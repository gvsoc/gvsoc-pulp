#
# Copyright (C) 2025 ETH Zurich and University of Bologna
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
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)

import gvsoc.systree
from typing import List, Tuple
from cache.cache import Cache
from interco.router import Router
from pulp.mempool.cache_filter import CacheFilter
import math


class Hierarchical_Interco(gvsoc.systree.Component):
    """
    Hierarchical interconnect for cluster AXI

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.


    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, bandwidth: int,
                 nb_slaves: int=1, nb_masters: int=1, enable_cache: bool=False, cache_rules: List[Tuple[int, int]]=[],
                 cache_line_width: int=64, cache_size: int=8192, nb_cache_sets: int=2):
        super(Hierarchical_Interco, self).__init__(parent, name)

        nb_ways = cache_size // (cache_line_width * nb_cache_sets)

        cache = Cache(self, 'cache', nb_sets_bits=int(math.log2(nb_cache_sets)), nb_ways_bits=int(math.log2(nb_ways)),
                      line_size_bits=int(math.log2(cache_line_width)), enabled=enable_cache)

        input_itf = Router(self, 'input_itf', bandwidth=bandwidth, latency=2)
        input_itf.add_mapping("output")

        filter = CacheFilter(self, 'filter', bypass=False, cache_rules=cache_rules, cache_latency=2)
        
        self.bind(input_itf, 'output', filter, 'input')
        self.bind(filter, 'cache', cache, 'input')
        
        self.bind(self, 'input', input_itf, 'input')
        self.bind(filter, 'bypass', self, 'output')
        self.bind(cache, 'refill', self, 'output')
        self.bind(self, 'rocache_cfg', filter, 'config')
