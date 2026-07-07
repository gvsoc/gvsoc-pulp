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

class IrqHold(gvsoc.systree.Component):
    """
    Rising-edge hold cell for the RedMulE done IRQ.

    The RedMulE `done` is a 1-cycle pulse (FINISHED->true, IDLE->false) that is OR-ed onto core 0's
    barrier wakeup. The core's barrier_sync ignores the wire value and acts on BOTH edges, latching a
    pending wakeup when the core is already awake. So the `done` FALLING edge injects one spurious
    wakeup that only the RedMulE cores get, which makes their next mempool_wfi (barrier) return without
    sleeping and desyncs them from the shared barrier -> hangs when completions spread out.

    This cell forwards only the RISING edge (sync(true), which wakes the sleeping core) and swallows
    the falling edge, so `done` produces exactly one wakeup and no phantom latch. Re-triggers still
    work: the FSM cycles IDLE->FINISHED, so the tracked input returns low and the next rising re-fires.
    """
    def __init__(self, parent: gvsoc.systree.Component, name: str):
        super().__init__(parent, name)

        self.add_sources(['pulp/mempool/irq_hold.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='wire<bool>')

    def o_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('output', itf, signature='wire<bool>')
