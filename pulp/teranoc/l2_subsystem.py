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
                 nb_banks: int, bank_width: int, size: int, port_bandwidth: int):
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
                            latency=1, atomics=True)
            l2_banks.append(l2_bank)

        input_itfs = []
        for i in range(0, nb_banks):
            input_itf = Router(self, f'input_itf_{i}', bandwidth=port_bandwidth)
            input_itf.add_mapping('output')
            input_itfs.append(input_itf)

        loader_router = Router(self, 'loader_router')
        for i in range(0, nb_banks):
            loader_router.add_mapping(f'bank_{i}', base=i*bank_size, size=bank_size, remove_offset=i*bank_size)

        #
        # Bindings
        #

        for i in range(0, nb_banks):
            self.bind(input_itfs[i], 'output', l2_banks[i], 'input')
            self.bind(loader_router, f'bank_{i}', l2_banks[i], 'input')

        for i in range(0, nb_banks):
            self.bind(self, 'input_%d' % i, input_itfs[i], 'input')

        self.bind(self, 'input_loader', loader_router, 'input')

    def i_BANK_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'input_{port}', signature='io')