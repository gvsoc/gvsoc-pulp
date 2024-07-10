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
from memory.memory import Memory
from interco.router import Router
from interco.converter import Converter
from pulp.wmem.wmem_interleaver import Wmem_interleaver
import math


class Wmem_subsystem(st.Component):

    def __init__(self, parent, name, cluster):
        super(Wmem_subsystem, self).__init__(parent, name)

        #
        # Properties
        #
        nb_wmem_banks = cluster.get_property('wmem/properties/nb_banks')
        wmem_bank_size = int(cluster.get_property('wmem/mapping/size', int) / nb_wmem_banks)

        #
        # Components
        #

        ico = Router(self, 'ico', latency=0)
        interleaver = Wmem_interleaver(self, 'interleaver', nb_masters=1, nb_slaves=nb_wmem_banks, stage_bits=0) # allow to extract the stage_bits from number of slaves

        wmem_banks = []
        for i in range(0, nb_wmem_banks):
            wmem_banks.append(Memory(self, f'bank{i}', size=wmem_bank_size))

        #
        # Bindings
        #

        ico.add_mapping('wmem_translated_address', **cluster._reloc_mapping(cluster.get_property('wmem/mapping')))
        self.bind(ico, 'wmem_translated_address', interleaver, 'in_0')

        for i in range(0, nb_wmem_banks):
            self.bind(interleaver, f'out_{i}', wmem_banks[i], 'input')

        self.bind(self, 'input', ico, 'input')