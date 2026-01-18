#
# Copyright (C) 2020 GreenWaves Technologies
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

import gvsoc.systree as st
import memory.memory as memory
import interco.interleaver as interleaver
import interco.testandset
from gvrun.attribute import Tree, Area, Value
from gvsoc.systree import IoAccuracy


class L2SharedAttr(Tree):
    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.nb_banks = Value(self, 'nb_banks', 4)
        self.nb_regions = Value(self, 'nb_regions', 12)
        self.interleaving_bits = Value(self, 'interleaving_bits', 2)
        self.range = Area(self, 'range', 0x1C010000, 0x00180000, description='L2 shared banks')

class L2Attr(Tree):
    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.range = Area(self, 'range', 0x1C000000, 0x00190000, description='L2 whole address range')
        self.priv0 = Area(self, 'priv0', 0x1C000000, 0x00008000, description='L2 private bank 0')
        self.priv1 = Area(self, 'priv1', 0x1C008000, 0x00008000, description='L2 private bank 1')
        self.shared = L2SharedAttr(self, 'shared')


class L2Subsystem(st.Component):

    def __init__(self, parent, name, attr):
        super(L2Subsystem, self).__init__(parent, name)

        l2_priv0 = memory.Memory(self, 'priv0', size=attr.priv0.size, power_trigger=True)
        l2_priv1 = memory.Memory(self, 'priv1', size=attr.priv1.size)

        l2_shared_size = attr.shared.range.size

        l2_shared_nb_banks = attr.shared.nb_banks
        l2_shared_nb_regions = attr.shared.nb_regions
        cut_size = int(l2_shared_size / l2_shared_nb_regions / l2_shared_nb_banks)

        if self.get_io_accuracy() != IoAccuracy.ACCURATE:
            for i in range(0, l2_shared_nb_regions):

                l2_shared_interleaver = interleaver.Interleaver(self, 'l2_shared_%d' % i, nb_slaves=l2_shared_nb_banks,
                    interleaving_bits=2)

                self.bind(self, 'shared_%d' % i, l2_shared_interleaver, 'input')

                for j in range(0, l2_shared_nb_banks):

                    cut = memory.Memory(self, 'shared_%d_cut_%d' % (i, j), size=cut_size)

                    self.bind(l2_shared_interleaver, 'out_%d' % j, cut, 'input')

        self.bind(self, 'priv0', l2_priv0, 'input')
        self.bind(self, 'priv1', l2_priv1, 'input')
