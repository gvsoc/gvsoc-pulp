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
from gvsoc.signature import IoV2Sync


class SoftHierCtrlConfig(Config):
    """Configuration of the SoftHier SoC control registers."""

    num_cluster: int = cfg_field(default=1, dump=True, desc="Number of clusters")
    num_core_per_cluster: int = cfg_field(default=1, dump=True, desc=(
        "Number of cores per cluster: the simulation stops once all the cores "
        "reported their end of computation"
    ))


class SoftHierCtrl(gvsoc.systree.Component):
    """SoftHier SoC control registers (end of computation, timer, output)."""

    def __init__(self, parent: gvsoc.systree.Component, name: str, config: SoftHierCtrlConfig):
        super().__init__(parent, name, config=config)

        self.add_sources(['pulp/chips/softhier_v2/common/softhier_ctrl.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Sync())
