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
from config_tree import Config, cfg_field


class KillModuleConfig(Config):
    """Configuration of the MAGIA kill module."""

    kill_addr_base: int = cfg_field(default=0, fmt="hex", desc=(
        "Base address of the kill registers"
    ))
    kill_addr_size: int = cfg_field(default=0, fmt="hex", desc=(
        "Size of the kill register area"
    ))
    nb_cores_to_wait: int = cfg_field(default=0, desc=(
        "Number of cores whose end is waited for before ending the simulation"
    ))
    done_irq_enable: bool = cfg_field(default=False, desc=(
        "True if the done interrupt is raised instead of ending the simulation"
    ))


class KillModule(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                kill_addr_base: int,
                kill_addr_size: int,
                nb_cores_to_wait: int,
                done_irq_enable: bool = False):

        super().__init__(parent, name, config=KillModuleConfig(
            kill_addr_base=kill_addr_base, kill_addr_size=kill_addr_size,
            nb_cores_to_wait=nb_cores_to_wait, done_irq_enable=done_irq_enable))

        self.add_sources(['pulp/chips/magia_v2/kill_module/kill_module.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_IRQ_DONE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq_done', itf, signature='wire<bool>')