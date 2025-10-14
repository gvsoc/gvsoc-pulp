# Copyright (C) 2025 Fondazione Chips-IT

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree

class KillModule(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                kill_addr_base: int,
                kill_addr_size: int,
                nb_cores_to_wait: int):

        super().__init__(parent, name)

        self.add_properties({
            'kill_addr_base' : kill_addr_base,
            'kill_addr_size' : kill_addr_size,
            'nb_cores_to_wait' : nb_cores_to_wait,
        })

        self.add_sources(['pulp/chips/magia/kill_module.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')