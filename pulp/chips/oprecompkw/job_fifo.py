#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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
from config_tree import Config, cfg_field


class JobFifoConfig(Config):
    """Configuration of the oprecompkw job FIFO."""

    size: int = cfg_field(default=0, dump=True, desc="Number of FIFO entries")
    width: int = cfg_field(default=0, dump=True, desc="Width of a FIFO entry, in bits")


class JobFifo(gvsoc.systree.Component):
    """oprecompkw job FIFO: a FIFO of jobs pushed on 'fifo', popped on 'input'."""

    def __init__(self, parent: gvsoc.systree.Component, name: str, size: int = 0,
            width: int = 0):
        super().__init__(parent, name, config=JobFifoConfig(size=size, width=width))

        self.add_sources(['pulp/chips/oprecompkw/job_fifo_impl.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def i_FIFO(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'fifo', signature='io')

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq', itf, signature='wire<bool>')
