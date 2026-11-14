#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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
from config_tree import Config, cfg_field


class RiscvTapHartConfig(Config):
    """One hart reachable from the RISC-V debug TAP."""

    hart_id: int = cfg_field(default=0, desc="Hart ID")
    target: str = cfg_field(default='', desc="Name of the port connected to the hart")


class RiscvTapConfig(Config):
    """Configuration of the RISC-V debug TAP."""

    harts: list[RiscvTapHartConfig] = cfg_field(default_factory=list, desc=(
        "Harts reachable from the TAP"))


class Riscv_tap(st.Component):

    def __init__(self, parent, name, nb_harts=0, idcode=0, harts=[]):

        super(Riscv_tap, self).__init__(parent, name, config=RiscvTapConfig(
            harts=[RiscvTapHartConfig(hart_id=hart_id, target=target)
                for hart_id, target in harts]))

        self.add_sources(['pulp/adv_dbg_unit/riscv_dtm_impl.cpp'])

