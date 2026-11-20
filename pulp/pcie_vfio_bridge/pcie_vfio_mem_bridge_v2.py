#
# Copyright (C) 2025 Fondazione Chips-IT
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

# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree
from gvsoc.signature import IoV2SingleReq


class PCIeVfioMemBridgeV2(gvsoc.systree.Component):
    """io_v2 version of PCIeVfioMemBridge.

    Same VFIO device, BAR0 registers and wires as PCIeVfioMemBridge; the memory
    port uses the io_v2 single-request protocol. A DMA is split in requests of
    ``dma_chunk_bytes``, one in flight at a time.
    """

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                socket_path: str,
                bar0_size: int,
                dma_chunk_bytes: int=4096):

        super().__init__(parent, name)

        self.add_properties({
            'socket_path': socket_path,
            'bar0_size': bar0_size,
            'dma_chunk_bytes': dma_chunk_bytes,
        })

        self.set_component('pulp.pcie_vfio_bridge.pcie_vfio_mem_bridge_v2')

    def i_IRQ_DONE(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'done_irq', signature='wire<bool>')

    def o_MEM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('mem', itf, signature=IoV2SingleReq())

    def o_FETCH_ENABLE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('fetch_en', itf, signature='wire<bool>')

    def o_ENTRY_ADDR(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('entry_addr', itf, signature='wire<uint64_t>')
