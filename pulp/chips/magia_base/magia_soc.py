#
# Copyright (C) 2025 ETH Zurich, University of Bologna and Fondazione ChipsIT
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
import memory.memory
import vp.clock_domain
import utils.loader.loader

from pulp.chips.magia_base.magia_tile import MagiaTile
from pulp.chips.magia_base.magia_arch import MagiaArch

class MagiaSoc(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, binary):
        super().__init__(parent, name)

        loader_t0 = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)
        #loader_t1 = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        # Single clock domain
        clock = vp.clock_domain.Clock_domain(self, 'tile-clock',
                                             frequency=MagiaArch.TILE_CLK_FREQ)
        clock.o_CLOCK(self.i_CLOCK())

        # L2 base model for testing
        l2_mem = memory.memory.Memory(self, 'test-mem', size=MagiaArch.L2_SIZE)

        # Single tile
        t0 = MagiaTile(self, 'magia-tile-0', parser, tid=0)
        #t1 = MagiaTile(self, 'magia-tile-1', parser, tid=1)

        # Bindings
        t0.o_XBAR(l2_mem.i_INPUT())
        #t1.o_XBAR(l2_mem.i_INPUT())

        loader_t0.o_OUT(t0.i_LOADER())
        loader_t0.o_START(t0.i_FETCHEN())
        loader_t0.o_ENTRY(t0.i_ENTRY())

        #loader_t1.o_OUT(t1.i_LOADER())
        #loader_t1.o_START(t1.i_FETCHEN())
        #loader_t1.o_ENTRY(t1.i_ENTRY())
