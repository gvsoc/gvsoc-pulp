#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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
from pulp.snitch.snitch_cluster.snitch_cluster_functional import SnitchCluster
import interco.router as router

class Quadrant(gvsoc.systree.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)

        nb_clusters_per_quadrant = 4
        quadrant_base = 0x10000000

        ico = router.Router(self, 'ico')

        for cid in range(0, nb_clusters_per_quadrant):
            cluster_size = 0x40000
            cluster_base = quadrant_base + cluster_size * cid
            cluster = SnitchCluster(self, f'cluster_{cid}')
            ico.o_MAP(cluster.i_INPUT(), base=cluster_base, size=cluster_size, rm_base=False)
            cluster.o_SOC(ico.i_INPUT())

        self.bind(self, 'input', ico, 'input')

        ico.o_MAP(self.i_SOC())


    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def i_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'soc', signature='io')

    def o_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('soc', itf, signature='io')

    def o_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('input', itf, signature='io')
