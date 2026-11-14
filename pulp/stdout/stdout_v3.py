#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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


class StdoutConfig(Config):
    """Configuration of the PULP stdout peripheral."""

    max_cluster: int = cfg_field(default=33, desc=(
        "Number of clusters which can print"
    ))
    max_core_per_cluster: int = cfg_field(default=16, desc=(
        "Number of cores per cluster which can print"
    ))
    user_set_core_id: int = cfg_field(default=0xdeadbeef, fmt="hex", desc=(
        "Core ID given by the user, 0xdeadbeef if none"
    ))
    user_set_cluster_id: int = cfg_field(default=0xdeadbeef, fmt="hex", desc=(
        "Cluster ID given by the user, 0xdeadbeef if none"
    ))


class Stdout(gvsoc.systree.Component):

    def __init__(self,
                 parent,
                 name,
                 max_cluster=33,
                 max_core_per_cluster=16,
                 user_set_core_id=0xdeadbeef,
                 user_set_cluster_id=0xdeadbeef):

        super(Stdout, self).__init__(parent, name, config=StdoutConfig(
            max_cluster=max_cluster, max_core_per_cluster=max_core_per_cluster,
            user_set_core_id=user_set_core_id, user_set_cluster_id=user_set_cluster_id))

        self.add_sources(['pulp/stdout/stdout_v3_impl.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
