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
from gvsoc.signature import IoV2Beat
from pulp.snitch.snitch_cluster.snitch_v2_cluster import SnitchCluster
import interco.router_v2 as router_v2
from interco.router_v2 import RouterConfig, RouterMapping, KIND_BEAT


class Quadrant(gvsoc.systree.Component):

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        #
        # Components
        #

        # Narrow 64bits router
        narrow_axi = router_v2.Router(self, 'narrow_axi', config=RouterConfig(
            kind=KIND_BEAT, width=8, max_pending_bursts_per_input=4))

        # Wide 512 bits router
        wide_axi = router_v2.Router(self, 'wide_axi', config=RouterConfig(
            kind=KIND_BEAT, width=64, max_pending_bursts_per_input=4))

        # Clusters
        clusters = []
        for cid in range(0, arch.nb_cluster):
            clusters.append(SnitchCluster(self, f'cluster_{cid}', arch.get_cluster(cid)))

        #
        # Bindings
        #

        # Input 0 of each router comes from the SoC, the next ones from the clusters

        # Narrow 64bits router
        self.o_NARROW_INPUT(narrow_axi.i_INPUT(0))
        narrow_axi.o_MAP_DEFAULT(self.i_NARROW_SOC(), name='soc')
        for cid in range(0, arch.nb_cluster):
            narrow_axi.o_MAP(clusters[cid].i_NARROW_INPUT(),
                RouterMapping(base=arch.get_cluster_base(cid), size=arch.cluster.size,
                    remove_base=False), name=f'cluster_{cid}')

        # Wide 512 bits router
        self.o_WIDE_INPUT(wide_axi.i_INPUT(0))
        wide_axi.o_MAP_DEFAULT(self.i_WIDE_SOC(), name='soc')
        for cid in range(0, arch.nb_cluster):
            wide_axi.o_MAP(clusters[cid].i_WIDE_INPUT(),
                RouterMapping(base=arch.get_cluster_base(cid), size=arch.cluster.size,
                    remove_base=False), name=f'cluster_{cid}')

        # Clusters
        for cid in range(0, arch.nb_cluster):
            clusters[cid].o_NARROW_SOC(narrow_axi.i_INPUT(cid + 1))
            clusters[cid].o_WIDE_SOC(wide_axi.i_INPUT(cid + 1))

        self.clusters = clusters

    def handle_executable(self, binary):
        for cluster in self.clusters:
            cluster.handle_executable(binary)

    def i_NARROW_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_soc', signature=IoV2Beat(8))

    def o_NARROW_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_soc', itf, signature=IoV2Beat(8))

    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature=IoV2Beat(8))

    def o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature=IoV2Beat(8), composite_bind=True)

    def i_WIDE_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_soc', signature=IoV2Beat(64))

    def o_WIDE_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_soc', itf, signature=IoV2Beat(64))

    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input', signature=IoV2Beat(64))

    def o_WIDE_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_input', itf, signature=IoV2Beat(64), composite_bind=True)
