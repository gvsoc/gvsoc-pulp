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

import gvsoc.systree
from config_tree import Config, cfg_field


class L1NocItfConfig(Config):
    nb_req_ports: int = cfg_field(default=0, desc="Number of request ports")
    nb_resp_ports: int = cfg_field(default=0, desc="Number of response ports")
    tile_id: int = cfg_field(default=0, desc="Identifier of the tile")
    group_id_x: int = cfg_field(default=0, desc="X position of the group")
    group_id_y: int = cfg_field(default=0, desc="Y position of the group")
    nb_x_groups: int = cfg_field(default=0, desc="Number of groups along X")
    nb_y_groups: int = cfg_field(default=0, desc="Number of groups along Y")
    byte_offset: int = cfg_field(default=0, desc="Number of address bits of the byte offset in a bank word")
    num_tiles_per_group: int = cfg_field(default=0, desc="Number of tiles per group")
    num_banks_per_tile: int = cfg_field(default=0, desc="Number of banks per tile")


class L1_NocItf(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str, nb_req_ports: int=2, nb_resp_ports: int=2,
                    tile_id: int=0, group_id_x: int=0, group_id_y: int=0, nb_x_groups: int=0, nb_y_groups: int=0,
                    byte_offset: int=2, num_tiles_per_group: int = 0, num_banks_per_tile: int = 0):
        super().__init__(parent, name, config=L1NocItfConfig(nb_req_ports=nb_req_ports,
            nb_resp_ports=nb_resp_ports, tile_id=tile_id, group_id_x=group_id_x,
            group_id_y=group_id_y, nb_x_groups=nb_x_groups, nb_y_groups=nb_y_groups,
            byte_offset=byte_offset, num_tiles_per_group=num_tiles_per_group,
            num_banks_per_tile=num_banks_per_tile))

        self.add_sources(['pulp/teranoc/l1_interconnect/l1_noc_itf.cpp'])
