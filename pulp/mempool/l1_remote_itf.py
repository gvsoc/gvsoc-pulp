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

class L1_RemoteItf(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str, req_latency: int=0, resp_latency: int=0, bandwidth: int=0, shared_rw_bandwidth: bool=True, synchronous: bool=True):
        super(L1_RemoteItf, self).__init__(parent, name)

        self.add_property('bandwidth', bandwidth)
        self.add_property('req_latency', req_latency)
        self.add_property('resp_latency', resp_latency)
        self.add_property('shared_rw_bandwidth', shared_rw_bandwidth)

        if synchronous:
            self.add_sources(['pulp/mempool/l1_remote_itf.cpp'])
        else:
            self.add_sources(['pulp/mempool/l1_remote_itf_async.cpp'])
