#
# Copyright (C) 2025 ETH Zurich and University of Bologna
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

class L1NocEndpointRouter(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, req_mode: bool=True, nb_tiles_per_group: int=16, num_banks_per_tile: int = 0, byte_offset: int=2):

        super().__init__(parent, name)

        self.add_sources(['pulp/teranoc/l1_interconnect/l1_noc_endpoint_router.cpp'])

        self.add_property("req_mode", req_mode)
        self.add_property("nb_tiles_per_group", nb_tiles_per_group)
        self.add_property("num_banks_per_tile", num_banks_per_tile)
        self.add_property("byte_offset", byte_offset)