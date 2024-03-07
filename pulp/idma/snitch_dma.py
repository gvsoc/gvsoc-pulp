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

class SnitchDma(gvsoc.systree.Component):
    """
    Snitch DMA

    This can be instantiated to handle memory transfers inside a Snitch cluster.

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    transfer_queue_size: int
        Number of transfer requests which can be queued to the DMA.
    burst_queue_size: int
        Maximum number of outstanding burst requests.
    loc_base: int
        Base address of the local area.
    loc_size: int
        Size of the local area.
    tcdm_width: int
        Width of the local interconnect, in bytes.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str,
            transfer_queue_size: int=8,
            burst_queue_size: int=8,
            loc_base: int=0,
            loc_size: int=0,
            tcdm_width: int=0):

        super().__init__(parent, name)

        self.add_sources([
            'pulp/idma/snitch_dma.cpp',
            'pulp/idma/fe/idma_fe_xdma.cpp',
            'pulp/idma/me/idma_me_2d.cpp',
            'pulp/idma/be/idma_be.cpp',
            'pulp/idma/be/idma_be_axi.cpp',
            'pulp/idma/be/idma_be_tcdm.cpp',
        ])

        self.add_properties({
            "transfer_queue_size": transfer_queue_size,
            "burst_queue_size": burst_queue_size,
            "loc_base": loc_base,
            "loc_size": loc_size,
            "tcdm_width": tcdm_width,
        })

    def i_OFFLOAD(self) -> gvsoc.systree.SlaveItf:
        """Returns the offload port.

        This is used by the core to offload xdma instructions.\n

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, 'offload', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT(self, itf: gvsoc.systree.SlaveItf):
        """Binds the offload grant port.

        This port is used for granting dmcpy instruction which was previously blocked because
        the queue was full.\n

        Parameters
        ----------
        slave: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind('offload_grant', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_AXI(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI port.

        This port is used for sending burst requests to the AXI interconnect.\n

        Parameters
        ----------
        slave: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind('axi', itf, signature='io')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        """Binds the TCDM port.

        This port is used for sending line requests to the TCDM memory.\n

        Parameters
        ----------
        slave: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind('tcdm', itf, signature='io')
