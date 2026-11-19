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
from config_tree import Config, cfg_field


class L1NocEndpointRouterConfig(Config):
    req_mode: bool = cfg_field(default=False, desc="True if the router routes requests, false for responses")
    nb_tiles_per_group: int = cfg_field(default=0, desc="Number of tiles per group")
    num_banks_per_tile: int = cfg_field(default=0, desc="Number of banks per tile")
    byte_offset: int = cfg_field(default=0, desc="Number of address bits of the byte offset in a bank word")


class L1NocEndpointRouter(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, req_mode: bool=True, nb_tiles_per_group: int=16, num_banks_per_tile: int = 0, byte_offset: int=2):

        super().__init__(parent, name, config=L1NocEndpointRouterConfig(req_mode=req_mode,
            nb_tiles_per_group=nb_tiles_per_group, num_banks_per_tile=num_banks_per_tile,
            byte_offset=byte_offset))

        self.add_sources(['pulp/teranoc/l1_interconnect/l1_noc_endpoint_router.cpp'])

