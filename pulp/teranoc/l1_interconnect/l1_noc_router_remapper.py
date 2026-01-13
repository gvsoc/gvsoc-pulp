#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
#

import gvsoc.systree

class L1NocRouterRemapper(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, nb_ports: int=2, remap_group_size: int=1, interleaved: bool=False):

        super().__init__(parent, name)

        self.add_sources(['pulp/teranoc/l1_interconnect/l1_noc_router_remapper.cpp'])

        self.add_property("nb_ports", nb_ports)
        self.add_property("remap_group_size", remap_group_size)
        self.add_property("interleaved", interleaved)
