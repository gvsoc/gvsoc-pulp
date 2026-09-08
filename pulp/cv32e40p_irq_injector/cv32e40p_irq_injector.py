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

# CV32E40P interrupt-line injector: lets the external co-simulation bridge
# drive the core interrupt wires (msi/mti/mei + fast irq16..31) through
# gv::wire_bind. One master wire port per line, named after the core slave
# port it drives.

import gvsoc.systree

# Line names and their interrupt numbers, index-aligned, in RVVI net order
# (MSWInterrupt, MTimerInterrupt, MExternalInterrupt, LocalInterrupt0..15).
IRQ_LINES: tuple = ('msi', 'mti', 'mei',
                    *(f'external_irq_{i}' for i in range(16, 32)))
IRQ_NUMBERS: tuple = (3, 7, 11, *range(16, 32))


class Cv32e40pIrqInjector(gvsoc.systree.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.add_sources(['pulp/cv32e40p_irq_injector/cv32e40p_irq_injector.cpp'])

    def o_LINE(self, name: str, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(name, itf, signature='wire<bool>')
