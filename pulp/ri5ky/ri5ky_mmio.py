#
# Copyright (C) 2026 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#

import gvsoc.systree


class Ri5kyMmio(gvsoc.systree.Component):
    """Minimal MMIO peripheral matching hw/ri5ky_gwt/gv_tb/mmio.sv:
    putchar @ +0x0, exit @ +0x4.
    """

    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.add_sources(['pulp/ri5ky/ri5ky_mmio.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io_v2')
