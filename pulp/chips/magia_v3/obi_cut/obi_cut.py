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


class ObiCut(gvsoc.systree.Component):
    """RTL obi_cut in front of a register slave.

    Takes single-beat accesses from the beat plane of a crossbar and sends
    them to an IoV2SingleReq slave one cycle later (spill register of the A
    channel); the response goes back one cycle after the slave answered (spill
    register of the R channel), plus ``resp_latency`` for a slave whose
    response is registered. A slave answering inline, like a register file with
    a combinational rvalid, thus costs two cycles, as in the RTL, where a cut
    followed by the framework beat-to-single-req adapter costs three.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, width: int,
            resp_latency: int=0):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia_v3/obi_cut/obi_cut.cpp'])
        self.add_properties({
            'width': width,
            'resp_latency': resp_latency,
        })
        self.width = width

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Beat(self.width))

    def o_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('output', itf, signature=IoV2SingleReq())
