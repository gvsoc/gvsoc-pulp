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


class ImaV1Config(Config):
    """Configuration of the IMA v1 in-memory accelerator (ima_v1_impl.cpp)."""

    nb_masters: int = cfg_field(default=0, desc="Number of master ports towards the memory")
    xbar_x: int = cfg_field(default=0, desc="Number of columns of the crossbar")
    xbar_y: int = cfg_field(default=0, desc="Number of rows of the crossbar")
    eval_ns: int = cfg_field(default=0, desc="Duration of an evaluation, in ns")
    plot_write_ns: int = cfg_field(default=0, desc="Duration of a plot write, in ns")
    plot_read_ns: int = cfg_field(default=0, desc="Duration of a plot read, in ns")
    statistics: bool = cfg_field(default=False, desc="True if statistics are reported")


class ImaV1(gvsoc.systree.Component):
    """IMA v1 in-memory accelerator."""

    def __init__(self, parent: gvsoc.systree.Component, name: str, config: ImaV1Config):
        super().__init__(parent, name, config=config)

        self.add_sources(['pulp/ima/ima_v1_impl.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_OUT(self, port: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'out_{port}', itf, signature='io')

    def o_IRQ(self, irq: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'irq_{irq}', itf, signature='wire<bool>')
