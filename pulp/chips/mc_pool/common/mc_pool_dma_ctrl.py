#
# Copyright (C) 2024 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
#

import gvsoc.systree

class McPoolDmaCtrl(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str):

        super().__init__(parent, name)

        self.add_sources(['pulp/chips/mc_pool/common/mc_pool_dma_ctrl.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
