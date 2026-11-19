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

import gvsoc.systree
from typing import List, Tuple
from config_tree import Config, cfg_field


class CacheRuleConfig(Config):
    """One address range going through the cache."""

    start: int = cfg_field(default=0, fmt="hex", desc="Start address of the range")
    end: int = cfg_field(default=0, fmt="hex", desc="End address of the range")


class CacheFilterConfig(Config):
    bypass: bool = cfg_field(default=False, desc="True if every request goes to the bypass port")
    cache_rules: list[CacheRuleConfig] = cfg_field(default_factory=list, desc=(
        "Address ranges going through the cache"))
    cache_latency: int = cfg_field(default=0, desc="Latency of the cache path, in cycles")


class CacheFilter(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, bypass: bool = False, cache_rules: List[Tuple[int, int]]=[], cache_latency: int=0):

        super().__init__(parent, name, config=CacheFilterConfig(bypass=bypass,
            cache_rules=[CacheRuleConfig(start=start, end=end) for start, end in cache_rules],
            cache_latency=cache_latency))

        self.add_sources(['pulp/mempool/l2_interconnect/cache_filter.cpp'])
