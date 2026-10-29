#
# Copyright (C) 2026 Fondazione Chips-IT
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

# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree
from gvsoc.signature import IoV2SingleReq


class TileCtrl(gvsoc.systree.Component):
    """Control registers of a MAGIA v4 tile at 0x1700 (``tile_ctrl.cpp``).

    One window for the three RTL slaves behind the control demux: Spatz
    (offset 0x000, obi_slave_ctrl_spatz), the PULP cluster (0x040,
    obi_slave_ctrl_cluster) and the collectives (0x100, obi_slave_ctrl_coll,
    registers only). Accesses are answered inline.

    Parameters
    ----------
    parent : gvsoc.systree.Component
        Parent component.
    name : str
        Local name within ``parent``.
    nb_cluster_cores : int
        Cores of the PULP cluster, the count CLUSTER_READY waits for.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, nb_cluster_cores: int):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia_v4/tile_ctrl/tile_ctrl.cpp'])
        self.add_properties({'nb_cluster_cores': nb_cluster_cores})

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2SingleReq())

    def o_SPATZ_CLK_EN(self, itf: gvsoc.systree.SlaveItf):
        """Fetch enable of Spatz (SPATZ_CLK_EN)."""
        self.itf_bind('spatz_clock_en', itf, signature='wire<bool>')

    def o_SPATZ_START(self, itf: gvsoc.systree.SlaveItf):
        """Start of Spatz, a level (SPATZ_START)."""
        self.itf_bind('spatz_start', itf, signature='wire<bool>')

    def o_SPATZ_DONE(self, itf: gvsoc.systree.SlaveItf):
        """One-cycle pulse on a SPATZ_DONE write."""
        self.itf_bind('spatz_done', itf, signature='wire<bool>')

    def o_CLUSTER_FETCHEN(self, itf: gvsoc.systree.SlaveItf):
        """Fetch enable of the cluster cores (CLUSTER_FETCH_EN)."""
        self.itf_bind('cluster_fetchen', itf, signature='wire<bool>')

    def o_CLUSTER_ENTRY(self, itf: gvsoc.systree.SlaveItf):
        """Boot address of the cluster cores (CLUSTER_BINARY)."""
        self.itf_bind('cluster_entry', itf, signature='wire<uint64_t>')

    def o_CLUSTER_DONE(self, itf: gvsoc.systree.SlaveItf):
        """One-cycle pulse on a CLUSTER_DONE write."""
        self.itf_bind('cluster_done', itf, signature='wire<bool>')

    def o_CLUSTER_START(self, itf: gvsoc.systree.SlaveItf):
        """One-cycle pulse on a non-zero CLUSTER_START write."""
        self.itf_bind('cluster_start', itf, signature='wire<bool>')
