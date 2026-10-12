#
# Copyright (C) 2025 Fondazione Chips-IT
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

# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree
from gvsoc.signature import IoV2Beat, IoV2SingleReq


class L2Port(gvsoc.systree.Component):
    """AXI port of the testbench L2 (axi_sim_mem).

    Takes bursts from the beat plane of the NoC and accesses the memory behind
    it, which must answer inline. A read returns its first beat after the
    latency the memory annotates (at least one cycle), then one beat per cycle;
    a write burst is acknowledged the same latency after its last beat.

    ``req_latency`` cycles are added before the memory, and ``max_reads``
    (0: no limit) bounds the read bursts in flight, the next one being taken
    the cycle after the last beat of a previous one.

    When ``i_AW`` is bound to the AW channel of the NoC, the first W beat of a
    burst is taken two cycles after its AW, as in axi_sim_mem.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, width: int,
            req_latency: int=0, max_reads: int=0):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia_v3/l2_port/l2_port.cpp'])
        self.add_properties({'width': width, 'req_latency': req_latency,
            'max_reads': max_reads})
        self.width = width

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Beat(self.width))

    def i_AW(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'aw', signature='wire<uint64_t>')

    def o_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('output', itf, signature=IoV2SingleReq())
