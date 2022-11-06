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

import gsystree as st
from ips.memory.memory import Memory
from ips.interco.router import Router
from ips.interco.converter import Converter
from pulp.ips.wmem.wmem_interleaver import Wmem_interleaver
import math


class Wmem_subsystem(st.Component):

    def __init__(self, parent, name, cluster):
        super(Wmem_subsystem, self).__init__(parent, name)

        #
        # Properties
        #

        nb_pe = cluster.get_property('nb_pe', int)
        l1_banking_factor = cluster.get_property('l1/banking_factor')
        nb_wmem_banks = 1<<int(math.log(nb_pe * l1_banking_factor, 2.0))
        wmem_bank_size = int(cluster.get_property('wmem/size', int) / nb_wmem_banks)

        #
        # Components
        #

        ico = Router(self, 'ico', latency=2)
        interleaver = Wmem_interleaver(self, 'interleaver', nb_masters=1, nb_slaves=nb_wmem_banks, stage_bits=2)

        wmem_banks = []
        for i in range(0, nb_wmem_banks):
            wmem_banks.append(Memory(self, f'bank{i}', size=wmem_bank_size))

        #
        # Bindings
        #

        ico.add_mapping('wmem_translated_address', **cluster._reloc_mapping(cluster.get_property('wmem')), id=0)
        self.bind(ico, 'wmem_translated_address', interleaver, 'in_0')

        for i in range(0, nb_wmem_banks):
            self.bind(interleaver, f'out_{i}', wmem_banks[i], 'input')

        self.bind(self, 'input', ico, 'input')