#
# Copyright (C) 2024 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
#

import gvsoc.systree
from pulp.idma.snitch_dma import SnitchDma
from pulp.chips.mc_pool.mempool_dma.flex_mempool_dma_ctrl import FlexMemPoolDmaCtrl

class FlexMemPoolDma(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str,
            transfer_queue_size: int=8,
            burst_queue_size: int=8,
            loc_base: int=0,
            loc_size: int=0,
            tcdm_width: int=0,
            tcdm_outstanding: int=1):

        super().__init__(parent, name)

        self.ctrl = FlexMemPoolDmaCtrl(self, 'flex_mempool_dma_ctrl')
        self.snitch_dma = SnitchDma(self, 'snitch_dma',
                                    transfer_queue_size=transfer_queue_size,
                                    burst_queue_size=burst_queue_size,
                                    loc_base=loc_base, loc_size=loc_size,
                                    tcdm_width=tcdm_width,
                                    tcdm_outstanding=tcdm_outstanding)

        self.bind(self.ctrl, 'dma_offload', self.snitch_dma, 'offload')

        self.bind(self, 'input', self.ctrl, 'input')

        self.bind(self.snitch_dma, 'axi_read', self, 'axi_read')
        self.bind(self.snitch_dma, 'axi_write', self, 'axi_write')
        self.bind(self.snitch_dma, 'tcdm_read', self, 'tcdm_read')
        self.bind(self.snitch_dma, 'tcdm_write', self, 'tcdm_write')
