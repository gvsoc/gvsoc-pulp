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

"""io_v2 port of the Snitch cluster zero memory.

Reads return zero, writes are acknowledged and dropped. The single
``input`` port always answers inline (:class:`IoV2Sync` contract).
"""

import gvsoc.systree
from gvsoc.signature import IoV2Sync
from config_tree import Config, cfg_field


class ZeroMemConfig(Config):
    """Configuration of the zero memory."""

    size: int = cfg_field(default=0, fmt="hex", desc=(
        "Size of the memory, accesses beyond it are errors"
    ))


class ZeroMem(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, size: int):

        super().__init__(parent, name, config=ZeroMemConfig(size=size))

        self.add_sources(['pulp/snitch/zero_mem_v2.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Sync())
