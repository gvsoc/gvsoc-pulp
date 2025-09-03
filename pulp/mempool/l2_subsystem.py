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
from memory.memory import Memory
from interco.router import Router
from interco.converter import Converter
from interco.interleaver import Interleaver
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
import math


class L2_subsystem(gvsoc.systree.Component):
    """
    Cluster L2 subsystem (memory banks + interconnects)

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    nb_banks: int
        The number of memory banks in the subsystem.
    bank_width: int
        The width of each memory bank in bytes.
    size: int
        The size of the memory in bytes.
    nb_masters: int
        The number of ports to access the memory.

    """

    def __init__(self, parent: gvsoc.systree.Component, name: str,
                 nb_banks: int, bank_width: int, size: int, nb_masters: int, port_bandwidth: int):
        super(L2_subsystem, self).__init__(parent, name)

        #
        # Properties
        #

        bank_size = size / nb_banks

        #
        # Components
        #

        l2_banks = []
        for i in range(0, nb_banks):
            l2_bank = Memory(self, 'l2_bank%d' % i, size=bank_size, width_log2=int(math.log(bank_width, 2.0)),
                            latency=3, atomics=True)
            l2_banks.append(l2_bank)

        l2_interleaver = Interleaver(self, 'l2_interleaver', nb_slaves=nb_banks, nb_masters=nb_masters+1,
                                      interleaving_bits=int(math.log2(bank_width)), offset_translation=True)

        input_itfs = []
        for i in range(0, nb_masters):
            input_itf = Router(self, f'input_itf_{i}', bandwidth=port_bandwidth)
            input_itf.add_mapping('output')
            input_itfs.append(input_itf)

        #
        # Bindings
        #

        for i in range(0, nb_banks):
            self.bind(l2_interleaver, 'out_%d' % i, l2_banks[i], 'input')

        for i in range(0, nb_masters):
            self.bind(self, 'input_%d' % i, input_itfs[i], 'input')
            self.bind(input_itfs[i], 'output', l2_interleaver, 'in_%d' % i)

        self.bind(self, 'input_loader', l2_interleaver, 'in_%d' % nb_masters)
