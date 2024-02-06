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

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        #
        # Components
        #

        # Main router
        ico = router.Router(self, 'ico')

        # Clusters
        clusters = []
        for cid in range(0, arch.nb_cluster):
            clusters.append(SnitchCluster(self, f'cluster_{cid}', arch.get_cluster(cid)))

        #
        # Bindings
        #

        # Main router
        self.o_INPUT(ico.i_INPUT())
        ico.o_MAP(self.i_SOC())
        for cid in range(0, arch.nb_cluster):
            ico.o_MAP(clusters[cid].i_INPUT(), base=arch.get_cluster_base(cid),
                size=arch.cluster.size, rm_base=False)

        # Clusters
        for cid in range(0, arch.nb_cluster):
            clusters[cid].o_SOC(ico.i_INPUT())



    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def i_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'soc', signature='io')

    def o_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('soc', itf, signature='io')

    def o_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('input', itf, signature='io', composite_bind=True)
