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
from gvsoc.signature import IoV2Sync
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
    nb_inputs: int = cfg_field(default=0, desc=(
        "Number of input ports, one per tile"
    ))


class KillModule(gvsoc.systree.Component):
    """Kill module: ends the simulation once every tile wrote its exit code.

    The input ports speak io_v2 and answer inline, hence the IoV2Sync
    signature. There is one port per tile (``i_INPUT(id)``) because an io_v2
    slave port binds exactly one master.
    """

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                kill_addr_base: int,
                kill_addr_size: int,
                nb_cores_to_wait: int,
                done_irq_enable: bool = False,
                nb_inputs: int = None):

        # One input port per tile: an io_v2 slave port binds exactly one master.
        if nb_inputs is None:
            nb_inputs = nb_cores_to_wait

        super().__init__(parent, name, config=KillModuleConfig(
            kill_addr_base=kill_addr_base, kill_addr_size=kill_addr_size,
            nb_cores_to_wait=nb_cores_to_wait, done_irq_enable=done_irq_enable,
            nb_inputs=nb_inputs))

        self.add_sources(['pulp/chips/magia_common/kill_module/kill_module.cpp'])

    def i_INPUT(self, id: int = 0) -> gvsoc.systree.SlaveItf:
        """Input port ``id`` (one per tile)."""
        return gvsoc.systree.SlaveItf(self, f'input_{id}', signature=IoV2Sync())

    def o_IRQ_DONE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq_done', itf, signature='wire<bool>')
