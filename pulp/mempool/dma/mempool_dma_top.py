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
from pulp.mempool.idma.mempool_dma import MemPoolDma
from pulp.mempool.dma.mempool_dma_ctrl import MemPoolDmaCtrl

class MemPoolDmaTop(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str,
            transfer_queue_size: int=8,
            burst_queue_size: int=8,
            burst_size: int=0,
            loc_base: int=0,
            loc_size: int=0,
            tcdm_width: int=0,
            nb_groups: int=4,
            nb_dmas_per_group: int=1,
            be_width: int=1024):

        super().__init__(parent, name)

        self.mempool_dma_ctrl = MemPoolDmaCtrl(self, 'mempool_dma_ctrl')
        self.mempool_dma = MemPoolDma(self, 'mempool_dma', transfer_queue_size=transfer_queue_size, burst_queue_size=burst_queue_size, \
                                      burst_size=burst_size, loc_base=loc_base, loc_size=loc_size, tcdm_width=tcdm_width, \
                                      nb_groups=nb_groups, nb_dmas_per_group=nb_dmas_per_group, be_width=be_width)

        self.bind(self.mempool_dma_ctrl, 'dma_offload', self.mempool_dma, 'offload')
        
        self.bind(self, 'input', self.mempool_dma_ctrl, 'input')

        for i in range(nb_groups):
            for j in range(nb_dmas_per_group):
                self.bind(self.mempool_dma, f'axi_read_{i}_{j}', self, f'axi_read_{i}_{j}')
                self.bind(self.mempool_dma, f'axi_write_{i}_{j}', self, f'axi_write_{i}_{j}')
                self.bind(self.mempool_dma, f'tcdm_read_{i}_{j}', self, f'tcdm_read_{i}_{j}')
                self.bind(self.mempool_dma, f'tcdm_write_{i}_{j}', self, f'tcdm_write_{i}_{j}')