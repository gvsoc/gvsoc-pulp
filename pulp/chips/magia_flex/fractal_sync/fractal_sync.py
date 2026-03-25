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

class FractalSync(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                level: int):

        super().__init__(parent, name)

        self.add_properties({
            'level' : level,
        })

        self.add_sources(['pulp/chips/magia/fractal_sync/fractal_sync.cpp'])

#we have 2 master ports
    def o_MASTER_EAST_WEST(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('master_ew_output_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_MASTER_EAST_WEST(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'master_ew_input_port', signature='wire<PortResp<uint32_t>*>')

    def o_MASTER_NORD_SUD(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('master_ns_output_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_MASTER_NORD_SUD(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'master_ns_input_port', signature='wire<PortResp<uint32_t>*>')

#we have 4 slave ports
    def i_SLAVE_NORD(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'slave_n_input_port', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_NORD(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('slave_n_output_port', itf, signature='wire<PortResp<uint32_t>*>')

    def i_SLAVE_SUD(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'slave_s_input_port', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_SUD(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('slave_s_output_port', itf, signature='wire<PortResp<uint32_t>*>')

    def i_SLAVE_EAST(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'slave_e_input_port', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_EAST(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('slave_e_output_port', itf, signature='wire<PortResp<uint32_t>*>')

    def i_SLAVE_WEST(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'slave_w_input_port', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_WEST(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('slave_w_output_port', itf, signature='wire<PortResp<uint32_t>*>')

    