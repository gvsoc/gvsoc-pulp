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
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)

import gvsoc.systree

class L1_NocItf(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str, nb_req_ports: int=2, nb_resp_ports: int=2,
                    tile_id: int=0, group_id_x: int=0, group_id_y: int=0, nb_x_groups: int=0, nb_y_groups: int=0,
                    byte_offset: int=2, num_tiles_per_group: int = 0, num_banks_per_tile: int = 0):
        super(L1_NocItf, self).__init__(parent, name)

        self.add_property('nb_req_ports', nb_req_ports)
        self.add_property('nb_resp_ports', nb_resp_ports)

        self.add_property('tile_id', tile_id)
        self.add_property('group_id_x', group_id_x)
        self.add_property('group_id_y', group_id_y)
        self.add_property('nb_x_groups', nb_x_groups)
        self.add_property('nb_y_groups', nb_y_groups)
        
        self.add_property('byte_offset', byte_offset)
        self.add_property('num_tiles_per_group', num_tiles_per_group)
        self.add_property('num_banks_per_tile', num_banks_per_tile)

        self.add_sources(['pulp/teranoc/l1_interconnect/l1_noc_itf.cpp'])
