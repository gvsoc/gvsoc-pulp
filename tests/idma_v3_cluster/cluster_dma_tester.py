#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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


class ClusterDmaTester(gvsoc.systree.Component):
    """Testbench driver for the two-stream cluster iDMA.

    Programs the transfers of the case one after the other through one
    register port (a read of NEXT_ID launches each one), waits until the
    DONE_ID of every stream has reached the last id it was given, counts the
    completion events, then reads every destination back and checks the
    pattern. Prints ``tester PASS idma_cycles=<n> ...`` on success.
    """

    def __init__(self, parent, name, *, regs_addr: int, transfers: list, nb_events: int,
                 nb_streams: int = 2, enable_gate: int = 0, quit_after_cycles: int = 1_000_000):
        super().__init__(parent, name)

        self.add_sources(['cluster_dma_tester.cpp'])

        self.add_property('regs_addr', regs_addr)
        self.add_property('transfers', transfers)
        self.add_property('nb_events', nb_events)
        self.add_property('nb_streams', nb_streams)
        self.add_property('enable_gate', enable_gate)
        self.add_property('quit_after_cycles', quit_after_cycles)

    def o_MEM(self, itf: gvsoc.systree.SlaveItf):
        """Single master for the register and memory accesses."""
        self.itf_bind('mem', itf, signature='io_v2')

    def o_ENABLE(self, itf: gvsoc.systree.SlaveItf):
        """Clock gate of the DMA, driven when the case exercises it."""
        self.itf_bind('enable', itf, signature='wire<bool>')

    def i_EVENT(self, core: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'event_{core}', signature='wire<bool>')

    def i_FC_EVENT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'fc_event', signature='wire<bool>')
