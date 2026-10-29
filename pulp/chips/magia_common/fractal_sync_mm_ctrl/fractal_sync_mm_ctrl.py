# Copyright (C) 2025 Fondazione Chips-IT

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree
from gvsoc.signature import IoV2Sync

class FSync_mm_ctrl(gvsoc.systree.Component):
    """Memory-mapped controller of the fractal sync network.

    The memory-mapped input port speaks io_v2 and answers inline, hence the
    IoV2Sync signature.

    As the RTL (obi_slave_fsync), a request carries the low ``aggr_width``
    bits of the aggregate register and the low ``id_width`` bits of the id
    register on the tree, ``nbr_aggr_width`` and ``nbr_id_width`` towards a
    neighbour node (the defaults keep the whole registers).
    """

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                aggr_width: int=32,
                id_width: int=32,
                nbr_aggr_width: int=32,
                nbr_id_width: int=32):

        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia_common/fractal_sync_mm_ctrl/fractal_sync_mm_ctrl.cpp'])
        self.add_properties({
            'aggr_width': aggr_width,
            'id_width': id_width,
            'nbr_aggr_width': nbr_aggr_width,
            'nbr_id_width': nbr_id_width,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Sync())
    
    # Fractal ports
    def o_XIF_2_FRACTAL_EAST_WEST(self, itf: gvsoc.systree.SlaveItf):
        #master port
        self.itf_bind('fractal_ew_input_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_FRACTAL_2_XIF_EAST_WEST(self) -> gvsoc.systree.SlaveItf:
        #slave port
        return gvsoc.systree.SlaveItf(self, 'fractal_ew_output_port', signature='wire<PortResp<uint32_t>*>')
    
    def o_XIF_2_FRACTAL_NORD_SUD(self, itf: gvsoc.systree.SlaveItf):
        #master port
        self.itf_bind('fractal_ns_input_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_FRACTAL_2_XIF_NORD_SUD(self) -> gvsoc.systree.SlaveItf:
        #slave port
        return gvsoc.systree.SlaveItf(self, 'fractal_ns_output_port', signature='wire<PortResp<uint32_t>*>')
    
    # Fractal neighbour ports
    def o_XIF_2_NEIGHBOUR_FRACTAL_EAST_WEST(self, itf: gvsoc.systree.SlaveItf):
        #master port
        self.itf_bind('neighbour_fractal_ew_input_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_NEIGHBOUR_FRACTAL_2_XIF_EAST_WEST(self) -> gvsoc.systree.SlaveItf:
        #slave port
        return gvsoc.systree.SlaveItf(self, 'neighbour_fractal_ew_output_port', signature='wire<PortResp<uint32_t>*>')
    
    def o_XIF_2_NEIGHBOUR_FRACTAL_NORD_SUD(self, itf: gvsoc.systree.SlaveItf):
        #master port
        self.itf_bind('neighbour_fractal_ns_input_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_NEIGHBOUR_FRACTAL_2_XIF_NORD_SUD(self) -> gvsoc.systree.SlaveItf:
        #slave port
        return gvsoc.systree.SlaveItf(self, 'neighbour_fractal_ns_output_port', signature='wire<PortResp<uint32_t>*>')
    
    def o_IRQ_FSYNC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('fsync_done_irq', itf, signature='wire<bool>')
    

def fsync_widths(n_tiles_x: int, n_tiles_y: int, n_tiles: int) -> dict:
    """Widths of the FractalSync requests of a tile (magia_pkg): the tree has
    FSYNC_LVL = clog2(N_TILES) levels on a square mesh, 2 otherwise; a tree
    request carries FSYNC_LVL + 1 bits of aggregate and max(FSYNC_LVL - 1, 2)
    bits of id, a neighbour request 1 and 2. Keyword arguments of
    FSync_mm_ctrl."""
    fsync_lvl = 2
    if n_tiles_x == n_tiles_y:
        fsync_lvl = (n_tiles - 1).bit_length()
    return {
        'aggr_width': fsync_lvl + 1,
        'id_width': max(fsync_lvl - 1, 2),
        'nbr_aggr_width': 1,
        'nbr_id_width': 2,
    }
