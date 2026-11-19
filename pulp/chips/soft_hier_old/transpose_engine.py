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

# Author: Chi Zhang <chizhang@iis.ee.ethz.ch>

import gvsoc.systree
from config_tree import Config, cfg_field


class TransposeEngineConfig(Config):
    """Configuration of the SoftHier (old) transpose engine."""

    tcdm_bank_width: int = cfg_field(default=0, dump=True, desc="Width of a TCDM bank, in bytes")
    tcdm_bank_number: int = cfg_field(default=0, dump=True, desc="Number of TCDM banks")
    queue_depth: int = cfg_field(default=16, dump=True, desc="Depth of the request queue")
    buffer_dim: int = cfg_field(default=32, dump=True, desc="Dimension of the transpose buffer")


class TransposeEngine(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                tcdm_bank_width: int,
                tcdm_bank_number: int,
                queue_depth: int=16,
                buffer_dim: int=32):

        super().__init__(parent, name, config=TransposeEngineConfig(
            tcdm_bank_width=int(tcdm_bank_width), tcdm_bank_number=int(tcdm_bank_number),
            queue_depth=int(queue_depth), buffer_dim=int(buffer_dim)))

        self.add_sources(['pulp/chips/soft_hier_old/transpose_engine.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('tcdm', itf, signature='io')
