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


class XdmaTester(gvsoc.systree.Component):
    """Testbench driver for the Snitch xdma-driven iDMA.

    Plays the role of the Snitch core: programs each transfer with dmsrc /
    dmdst / dmstr / dmrep and launches it with dmcpy over the offload wire,
    replaying a refused dmcpy every cycle until it is granted as the iss_v2
    core does; then polls dmstat(busy) until the DMA is idle, counts the irq
    pulses, and reads every destination back to check the pattern. Prints
    ``tester PASS idma_cycles=<n> ...`` on success.
    """

    def __init__(self, parent, name, *, transfers: list, quit_after_cycles: int = 1_000_000):
        super().__init__(parent, name)

        self.add_sources(['xdma_tester.cpp'])

        self.add_property('transfers', transfers)
        self.add_property('quit_after_cycles', quit_after_cycles)

    def o_OFFLOAD(self, itf: gvsoc.systree.SlaveItf):
        """The offload wire towards the DMA front-end."""
        self.itf_bind('offload', itf, signature='wire<IssOffloadInsn<uint32_t>*>')

    def i_OFFLOAD_GRANT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload_grant',
            signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_MEM(self, itf: gvsoc.systree.SlaveItf):
        """Master for the destination read-back."""
        self.itf_bind('mem', itf, signature='io_v2')

    def i_IRQ(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'irq', signature='wire<bool>')
