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
from pulp.idma.idma_config import IdmaConfig


class DistributedDma(gvsoc.systree.Component):
    """
    Distributed iDMA

    One front-end whose transfers are distributed over several back-ends, each owning a region of
    the local memory, as the MemPool iDMA (idma_split_midend + idma_distributed_midend):

      xdma front-end -> 2D middle-end -> split middle-end -> cluster distributed middle-end
        -> one distributed middle-end per group -> nb_dmas_per_group back-ends per group

    The local memory is cut into regions of be_width bytes, back-end (i, j) (group i, back-end j
    of the group) handling region i * nb_dmas_per_group + j, repeating every
    nb_groups * nb_dmas_per_group * be_width bytes. The back-ends run in parallel.

    Broadcasts and reductions are not supported.

    Back-end (i, j) has the ports axi_read_{i}_{j}, axi_write_{i}_{j} towards the external memory and
    tcdm_read_{i}_{j}, tcdm_write_{i}_{j} towards the local one.

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    transfer_queue_size: int
        Number of transfers which can be queued to each back-end.
    burst_queue_size: int
        Maximum number of outstanding burst requests of each back-end.
    burst_size: int
        Maximum burst size, in bytes, 0 for unconstrained.
    loc_base: int
        Base address of the local area.
    loc_size: int
        Size of the local area.
    tcdm_width: int
        Width of one TCDM access of a back-end, in bytes.
    nb_groups: int
        Number of groups of back-ends.
    nb_dmas_per_group: int
        Number of back-ends per group.
    be_width: int
        Size in bytes of the local region handled by one back-end.
    """

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

        super().__init__(parent, name, config=IdmaConfig(transfer_queue_size=transfer_queue_size,
            burst_queue_size=burst_queue_size, burst_size=burst_size, loc_base=loc_base,
            loc_size=loc_size, tcdm_width=tcdm_width, nb_groups=nb_groups,
            nb_dmas_per_group=nb_dmas_per_group, be_width=be_width))

        self.nb_groups = nb_groups
        self.nb_dmas_per_group = nb_dmas_per_group

        self.add_sources([
            'pulp/idma/distributed_dma.cpp',
            'pulp/idma/fe/idma_fe_xdma.cpp',
            'pulp/idma/me/idma_me_2d.cpp',
            'pulp/idma/me/idma_me_split.cpp',
            'pulp/idma/me/idma_me_dist.cpp',
            'pulp/idma/be/idma_be.cpp',
            'pulp/idma/be/idma_be_axi.cpp',
            'pulp/idma/be/idma_be_tcdm.cpp',
        ])

    def i_OFFLOAD(self) -> gvsoc.systree.SlaveItf:
        """Returns the offload port, used by the core to offload xdma instructions."""
        return gvsoc.systree.SlaveItf(self, 'offload', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT(self, itf: gvsoc.systree.SlaveItf):
        """Binds the offload grant port, used to grant a blocked xdma instruction."""
        self.itf_bind('offload_grant', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_AXI(self, group: int, dma: int, itf: gvsoc.systree.SlaveItf):
        """Binds the external ports of back-end dma of group group."""
        self.itf_bind(f'axi_read_{group}_{dma}', itf, signature='io')
        self.itf_bind(f'axi_write_{group}_{dma}', itf, signature='io')

    def o_TCDM(self, group: int, dma: int, itf: gvsoc.systree.SlaveItf):
        """Binds the local ports of back-end dma of group group."""
        self.itf_bind(f'tcdm_read_{group}_{dma}', itf, signature='io')
        self.itf_bind(f'tcdm_write_{group}_{dma}', itf, signature='io')
