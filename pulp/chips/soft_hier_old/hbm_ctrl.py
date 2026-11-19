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

import gvsoc.systree as st
from config_tree import Config, cfg_field


class HbmCtrlConfig(Config):
    """Configuration of the SoftHier (old) HBM controller (address interleaver)."""

    nb_slaves: int = cfg_field(default=0, dump=True, desc="Number of output ports (HBM channels)")
    nb_masters: int = cfg_field(default=0, dump=True, desc="Number of input ports")
    stage_bits: int = cfg_field(default=0, dump=True, desc="Number of address bits of a stage")
    interleaving_bits: int = cfg_field(default=0, dump=True, desc="Log2 of the interleaving granularity")
    node_addr_offset: int = cfg_field(default=0, fmt="hex", dump=True, desc="Address range of a node")
    hbm_node_aliase: int = cfg_field(default=0, dump=True, desc="Number of nodes aliased on the same channels")
    xor_scrambling: int = cfg_field(default=0, dump=True, desc="1 to enable XOR address scrambling")
    red_scrambling: int = cfg_field(default=0, dump=True, desc="1 to enable reduction address scrambling")


class hbm_ctrl(st.Component):

    def __init__(self, parent, slave, nb_slaves=0, nb_masters=0, stage_bits=0, interleaving_bits=2, node_addr_offset=0, hbm_node_aliase=0, xor_scrambling=0, red_scrambling=0):

        super(hbm_ctrl, self).__init__(parent, slave, config=HbmCtrlConfig(
            nb_slaves=nb_slaves, nb_masters=nb_masters, stage_bits=stage_bits,
            interleaving_bits=interleaving_bits, node_addr_offset=node_addr_offset,
            hbm_node_aliase=hbm_node_aliase, xor_scrambling=xor_scrambling,
            red_scrambling=red_scrambling))

        self.add_sources(['pulp/chips/soft_hier_old/hbm_ctrl.cpp'])
