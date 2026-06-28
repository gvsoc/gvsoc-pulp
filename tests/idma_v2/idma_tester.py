#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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


class IDmaTesterV2(gvsoc.systree.Component):
    """Model-level testbench driver for idma_v2.

    Fills a source memory range with a deterministic pattern, programs the
    iDMA registers, waits for the completion IRQ, then reads the destination
    range back and checks each byte against the same pattern. Calls
    engine->quit(0) on success and quit(1) on the first mismatch or timeout.
    """

    def __init__(self, parent, name, *,
                 regs_addr: int,
                 src: int, dst: int, length: int,
                 src_stride: int = 0, dst_stride: int = 0,
                 reps: int = 1, config: int = 0,
                 pattern_seed: int = 0xa5,
                 quit_after_cycles: int = 1_000_000):
        super().__init__(parent, name)
        self.add_sources(['idma_tester.cpp'])
        self.add_property('regs_addr',         regs_addr)
        self.add_property('src',               src)
        self.add_property('dst',               dst)
        self.add_property('length',            length)
        self.add_property('src_stride',        src_stride)
        self.add_property('dst_stride',        dst_stride)
        self.add_property('reps',              reps)
        self.add_property('config',            config)
        self.add_property('pattern_seed',      pattern_seed)
        self.add_property('quit_after_cycles', quit_after_cycles)

    def o_REGS(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('regs', itf, signature='io_v2')

    def o_MEM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('mem', itf, signature='io_v2')

    def i_IRQ(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'irq', signature='wire<bool>')
