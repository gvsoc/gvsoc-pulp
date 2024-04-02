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
from pulp.snitch.snitch_cluster.snitch_cluster import SnitchCluster
import interco.router as router

class Quadrant(gvsoc.systree.Component):

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        #
        # Components
        #

        # Narrow 64bits router
        narrow_axi = router.Router(self, 'narrow_axi', bandwidth=8)

        # Wide 512 bits router
        wide_axi = router.Router(self, 'wide_axi', bandwidth=64)

        # Clusters
        clusters = []
        for cid in range(0, arch.nb_cluster):
            clusters.append(SnitchCluster(self, f'cluster_{cid}', arch.get_cluster(cid)))

        #
        # Bindings
        #

        # Narrow 64bits router
        self.o_NARROW_INPUT(narrow_axi.i_INPUT())
        narrow_axi.o_MAP(self.i_NARROW_SOC())
        for cid in range(0, arch.nb_cluster):
            narrow_axi.o_MAP(clusters[cid].i_NARROW_INPUT(), base=arch.get_cluster_base(cid),
                size=arch.cluster.size, rm_base=False)

        # Wide 512 bits router
        self.o_WIDE_INPUT(wide_axi.i_INPUT())
        wide_axi.o_MAP(self.i_WIDE_SOC())
        for cid in range(0, arch.nb_cluster):
            wide_axi.o_MAP(clusters[cid].i_WIDE_INPUT(), base=arch.get_cluster_base(cid),
                size=arch.cluster.size, rm_base=False)

        # Clusters
        for cid in range(0, arch.nb_cluster):
            clusters[cid].o_NARROW_SOC(narrow_axi.i_INPUT())
            clusters[cid].o_WIDE_SOC(wide_axi.i_INPUT())



    def i_NARROW_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_soc', signature='io')

    def o_NARROW_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_soc', itf, signature='io')

    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature='io')

    def o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature='io', composite_bind=True)

    def i_WIDE_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_soc', signature='io')

    def o_WIDE_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_soc', itf, signature='io')

    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input', signature='io')

    def o_WIDE_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_input', itf, signature='io', composite_bind=True)
