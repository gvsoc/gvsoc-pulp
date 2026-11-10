#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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

"""Chimera SoC registers (chimera_reg_top), io_v2.

Register map of the chimera-sdk ``soc_regs.h``: 32-bit registers, any access
of 1 to 4 bytes within a register. For each cluster: return value, reset,
clock gate enable, wide memory bypass and busy, plus the Snitch boot and
interrupt handler addresses, which the software running on the clusters
reads back. They are storage only: resetting or gating a cluster has no
effect on the model.
"""

from __future__ import annotations

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync


class ChimeraRegsConfig(Config):
    nb_regs: int = cfg_field(default=28, dump=True, desc="Number of 32-bit registers")


class ChimeraRegs(Component):

    def __init__(self, parent: Component, name: str, config: ChimeraRegsConfig = None):
        if config is None:
            config = ChimeraRegsConfig()
        super().__init__(parent, name, config=config)

        self.add_sources(['pulp/chips/chimera_v2/chimera_regs.cpp'])

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2Sync())
