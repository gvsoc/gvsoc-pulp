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
from gvsoc.signature import IoV2SingleReq

class iDMA_mm_ctrl(gvsoc.systree.Component):
    """Memory-mapped controller of the two iDMA channels of a tile.

    The RTL idma_obi_ctrl_decoder: the first 0x200 bytes of the window are the
    idma_reg32_3d registers of the AXI to OBI channel (L2 to L1), the next
    0x200 the ones of the OBI to AXI channel (L1 to L2). Accesses are forwarded
    with the offset inside the channel; grants, responses and retries come back
    unchanged, so the ports are IoV2SingleReq (a launch may be denied).
    """

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str):

        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia_v3/idma_mm_ctrl/idma_mm_ctrl.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2SingleReq())

    def o_AXI2OBI(self, itf: gvsoc.systree.SlaveItf):
        """Binds the register port of the AXI to OBI channel."""
        self.itf_bind('axi2obi', itf, signature=IoV2SingleReq())

    def o_OBI2AXI(self, itf: gvsoc.systree.SlaveItf):
        """Binds the register port of the OBI to AXI channel."""
        self.itf_bind('obi2axi', itf, signature=IoV2SingleReq())
