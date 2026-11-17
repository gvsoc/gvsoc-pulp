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

import gvsoc.systree
from gvsoc.signature import IoV2Sync
from config_tree import Config, cfg_field


class Cva6tbRegsConfig(Config):
    """Configuration of the platform control registers of the CVA6 standalone testbench."""

    boot_mode: int = cfg_field(default=0, desc=(
        "Value of the read-only boot mode register: 0 boots from DRAM, 1 from the SPM"
    ))


class Cva6tbRegs(gvsoc.systree.Component):
    """Platform control registers of the CVA6 standalone testbench (test/hw/cva6tb_regs.sv).

    16 32-bit registers. 0x0: boot mode (read-only). 0x4: end of computation, bit 0 ends the
    simulation with bits 31:1 as the status. 0x8: external interrupt lines, bit 0 the M-mode
    one, bit 1 the S-mode one. 0xc: workload phase marker. The others are plain storage.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, boot_mode: int=0):
        super().__init__(parent, name, config=Cva6tbRegsConfig(boot_mode=boot_mode))

        self.add_sources(['pulp/cva6tb/cva6tb_regs.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Sync())

    def o_IRQ_M(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq_m', itf, signature='wire<bool>')

    def o_IRQ_S(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq_s', itf, signature='wire<bool>')
