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
from config_tree import Config, cfg_field


class L1RemoteItfConfig(Config):
    req_latency: int = cfg_field(default=0, desc="Latency applied to requests")
    resp_latency: int = cfg_field(default=0, desc="Latency applied to responses")
    bandwidth: int = cfg_field(default=0, desc="Bandwidth, in bytes per cycle")
    shared_rw_bandwidth: bool = cfg_field(default=False, desc="True if reads and writes share the bandwidth")
    throttle: int = cfg_field(default=0, desc="Throttling of the requests (synchronous version only)")


class L1_RemoteItf(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str, req_latency: int=0, resp_latency: int=0, bandwidth: int=0, shared_rw_bandwidth: bool=True, throttle: int=0, synchronous: bool=True):
        super().__init__(parent, name, config=L1RemoteItfConfig(req_latency=req_latency,
            resp_latency=resp_latency, bandwidth=bandwidth, shared_rw_bandwidth=shared_rw_bandwidth,
            throttle=throttle))

        if synchronous:
            self.add_sources(['pulp/mempool/l1_interconnect/l1_remote_itf.cpp'])
        else:
            self.add_sources(['pulp/mempool/l1_interconnect/l1_remote_itf_async.cpp'])
