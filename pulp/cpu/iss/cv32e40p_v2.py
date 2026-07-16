#
# Copyright (C) 2026 Fondazione Chips-it
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

#
# Authors: Marco Paci, Fondazione Chips-it (marco.paci@chips.it)
#

from __future__ import annotations

from typing import Iterable
from gvsoc.systree import Component
from cpu.iss_v2.riscv import (RiscvCommon, IrqExternal, ExecInOrder, Regfile,
                              Csr, Event, LsuV2, Hwloop)
from cpu.iss.isa_gen.isa_gen import Isa, IsaSubset
from cpu.iss.isa_gen.isa_riscv_gen import RiscvIsa
from cpu.iss.isa_gen.isa_cv32e40pv2 import CoreV2
from cpu.iss_v2.riscv_config import RiscvConfig

isa_instances: dict[tuple[str, str], Isa] = {}


class Cv32e40pConfig(RiscvConfig):
    pass


class Cv32e40p(RiscvCommon):
    """CV32E40P on the iss_v2 modular core.

    Bring-up recipe: generic v2 slots plus the CoreV ISA subset. The
    CV32E40P-specific CSR map, event counters and trap behaviour come in
    as dedicated slot overrides on top of this base (same layering as
    Ri5ky).
    """

    # Tag used in ISA-cache and generated ISA-class names (see Ri5ky).
    isa_name: str = 'cv32e40p_v2'

    def __init__(self, parent: Component, name: str, config: Cv32e40pConfig,
                 extra_extensions: Iterable[IsaSubset] = ()):

        cache_key = (type(self).isa_name, config.isa)
        isa_instance: Isa | None = isa_instances.get(cache_key)

        if isa_instance is None:
            extensions: list[IsaSubset] = [
                *extra_extensions,
                CoreV2(),
            ]

            isa_instance = RiscvIsa(f"{type(self).isa_name}_{config.isa}",
                config.isa, extensions=extensions)

            isa_instances[cache_key] = isa_instance

        modules: dict[str, object] = {
            'irq': IrqExternal(),
            'event': Event(),
            'csr': Csr(),
            'exec': ExecInOrder(scoreboard=True, inorder_commit=True),
            'lsu': LsuV2(),
            'regfile': Regfile(scoreboard=True),
            'hwloop': Hwloop(),
        }

        super().__init__(parent, name, config=config, isa=isa_instance,
                         modules=modules)
