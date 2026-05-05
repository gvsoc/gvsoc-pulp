#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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

class CheshireDmaV2(gvsoc.systree.Component):
    """
    Cheshire DMA (IO v2)

    Same shape as CheshireDma, but the IO ports speak the v2 IO protocol
    (vp/itf/io_v2.hpp) so this generator must be wired against v2 routers
    and v2 memories.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str,
            transfer_queue_size: int=8,
            burst_queue_size: int=8,
            loc_base: int=0,
            loc_size: int=0):

        super().__init__(parent, name)

        self.add_sources([
            'pulp/idma_v2/cheshire_dma.cpp',
            'pulp/idma_v2/fe/idma_fe_cheshire.cpp',
            'pulp/idma_v2/me/idma_me_2d.cpp',
            'pulp/idma_v2/be/idma_be.cpp',
            'pulp/idma_v2/be/idma_be_axi.cpp',
            'pulp/idma_v2/be/idma_be_tcdm.cpp',
        ])

        self.add_properties({
            "transfer_queue_size": transfer_queue_size,
            "burst_queue_size": burst_queue_size,
            "loc_base": loc_base,
            "loc_size": loc_size,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io_v2')

    def o_OFFLOAD_GRANT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('offload_grant', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_AXI(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('axi_read', itf, signature='io_v2')
        self.itf_bind('axi_write', itf, signature='io_v2')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('tcdm_read', itf, signature='io_v2')
        self.itf_bind('tcdm_write', itf, signature='io_v2')
