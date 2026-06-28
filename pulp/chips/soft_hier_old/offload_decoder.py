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


class SoftHierOldOffloadDecoder(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, nb_cores: int=1):
        super().__init__(parent, name)
        self.add_sources(['pulp/chips/soft_hier_old/offload_decoder.cpp'])
        self.add_properties({
            "nb_cores": nb_cores,
        })

    def i_OFFLOAD(self, core_id: int=0) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'offload_{core_id}', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT(self, core_id: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'offload_grant_{core_id}', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_DMA(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('dma_offload', itf, signature='wire<IssOffloadInsn<uint32_t>*>')

    def i_DMA_GRANT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'dma_offload_grant', signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_REDMULE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('redmule_offload', itf, signature='wire<IssOffloadInsn<uint32_t>*>')

    def i_REDMULE_GRANT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'redmule_offload_grant', signature='wire<IssOffloadInsnGrant<uint32_t>*>')
