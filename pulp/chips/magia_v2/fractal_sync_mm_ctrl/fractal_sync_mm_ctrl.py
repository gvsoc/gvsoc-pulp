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

class FSync_mm_ctrl(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str):

        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia_v2/fractal_sync_mm_ctrl/fractal_sync_mm_ctrl.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
    
    # Fractal ports
    def o_XIF_2_FRACTAL_EAST_WEST(self, itf: gvsoc.systree.SlaveItf):
        #master port
        self.itf_bind('fractal_ew_input_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_FRACTAL_2_XIF_EAST_WEST(self) -> gvsoc.systree.SlaveItf:
        #slave port
        return gvsoc.systree.SlaveItf(self, 'fractal_ew_output_port', signature='wire<PortResp<uint32_t>*>')
    
    def o_XIF_2_FRACTAL_NORD_SUD(self, itf: gvsoc.systree.SlaveItf):
        #master port
        self.itf_bind('fractal_ns_input_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_FRACTAL_2_XIF_NORD_SUD(self) -> gvsoc.systree.SlaveItf:
        #slave port
        return gvsoc.systree.SlaveItf(self, 'fractal_ns_output_port', signature='wire<PortResp<uint32_t>*>')
    
    # Fractal neighbour ports
    def o_XIF_2_NEIGHBOUR_FRACTAL_EAST_WEST(self, itf: gvsoc.systree.SlaveItf):
        #master port
        self.itf_bind('neighbour_fractal_ew_input_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_NEIGHBOUR_FRACTAL_2_XIF_EAST_WEST(self) -> gvsoc.systree.SlaveItf:
        #slave port
        return gvsoc.systree.SlaveItf(self, 'neighbour_fractal_ew_output_port', signature='wire<PortResp<uint32_t>*>')
    
    def o_XIF_2_NEIGHBOUR_FRACTAL_NORD_SUD(self, itf: gvsoc.systree.SlaveItf):
        #master port
        self.itf_bind('neighbour_fractal_ns_input_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_NEIGHBOUR_FRACTAL_2_XIF_NORD_SUD(self) -> gvsoc.systree.SlaveItf:
        #slave port
        return gvsoc.systree.SlaveItf(self, 'neighbour_fractal_ns_output_port', signature='wire<PortResp<uint32_t>*>')
    
    def o_IRQ_FSYNC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('fsync_done_irq', itf, signature='wire<bool>')
    