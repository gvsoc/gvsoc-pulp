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

class Magia_iDMA_Ctrl(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str):

        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia/idma_ctrl.cpp'])

    def i_OFFLOAD_M(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload_m', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT_M(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('offload_grant_m', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_OFFLOAD_iDMA0_AXI2OBI(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('offload_idma0_axi2obi', itf, signature='wire<IssOffloadInsn<uint32_t>*>')

    def i_OFFLOAD_GRANT_iDMA0_AXI2OBI(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload_grant_idma0_axi2obi', signature='wire<IssOffloadInsnGrant<uint32_t>*>')
    
    def o_OFFLOAD_iDMA1_OBI2AXI(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('offload_idma1_obi2axi', itf, signature='wire<IssOffloadInsn<uint32_t>*>')
    
    def i_OFFLOAD_GRANT_iDMA1_OBI2AXI(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload_grant_idma1_obi2axi', signature='wire<IssOffloadInsnGrant<uint32_t>*>')
    
    def o_IRQ_DMA0(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('idma0_done_irq', itf, signature='wire<bool>')

    def o_IRQ_DMA1(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('idma1_done_irq', itf, signature='wire<bool>')