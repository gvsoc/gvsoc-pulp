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
from config_tree import Config, cfg_field


class HWPEInterleaverConfig(Config):
    """Configuration of the SoftHier (old) HWPEInterleaver."""

    nb_banks: int = cfg_field(default=0, dump=True, desc="Number of TCDM banks")
    bank_width: int = cfg_field(default=0, dump=True, desc="Width of a TCDM bank, in bytes")


class HWPEInterleaver(gvsoc.systree.Component):

    def __init__(self, parent, slave, nb_master_ports, nb_banks, bank_width):

        super(HWPEInterleaver, self).__init__(parent, slave, config=HWPEInterleaverConfig(
            nb_banks=int(nb_banks), bank_width=int(bank_width)))

        self.add_sources(
            ['pulp/chips/soft_hier_old/hwpe_interleaver.cpp']
        )
