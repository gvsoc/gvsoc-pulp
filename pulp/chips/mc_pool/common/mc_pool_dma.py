#
# Copyright (C) 2024 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
#

import gvsoc.systree
from pulp.idma.distributed_dma import DistributedDma
from pulp.chips.mc_pool.common.mc_pool_dma_ctrl import McPoolDmaCtrl

class McPoolDma(gvsoc.systree.Component):
    """
    Cluster DMA of mc_pool

    The register front-end (McPoolDmaCtrl) offloads the transfers to the distributed iDMA of the
    single-cluster MemPool model: they are split over nb_groups x nb_dmas_per_group back-ends, each
    owning a region of be_width bytes of the L1 and accessing its tiles in parallel with the others.

    Back-end (i, j) has the ports axi_read_{i}_{j} / axi_write_{i}_{j} towards the NoC and
    tcdm_read_{i}_{j} / tcdm_write_{i}_{j} towards the L1.

    Broadcasts and reductions are not supported by the distributed iDMA.
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

        super().__init__(parent, name)

        self.nb_groups = nb_groups
        self.nb_dmas_per_group = nb_dmas_per_group

        self.ctrl = McPoolDmaCtrl(self, 'ctrl')
        self.idma = DistributedDma(self, 'idma',
                                   transfer_queue_size=transfer_queue_size,
                                   burst_queue_size=burst_queue_size,
                                   burst_size=burst_size,
                                   loc_base=loc_base, loc_size=loc_size,
                                   tcdm_width=tcdm_width,
                                   nb_groups=nb_groups,
                                   nb_dmas_per_group=nb_dmas_per_group,
                                   be_width=be_width)

        self.bind(self.ctrl, 'dma_offload', self.idma, 'offload')

        self.bind(self, 'input', self.ctrl, 'input')

        for i in range(nb_groups):
            for j in range(nb_dmas_per_group):
                for port in ['axi_read', 'axi_write', 'tcdm_read', 'tcdm_write']:
                    self.bind(self.idma, f'{port}_{i}_{j}', self, f'{port}_{i}_{j}')
