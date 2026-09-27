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


import gvsoc.systree
from config_tree import Config, cfg_field
from gvsoc.signature import IoV2Sync


class ClusterCsrConfig(Config):
    """Configuration of the SoftHier cluster control registers."""

    nb_cores: int = cfg_field(default=1, dump=True, desc="Number of cores of the cluster")
    cluster_id: int = cfg_field(default=0, dump=True, desc="Cluster ID, read at offset 0")


class ClusterCSR(gvsoc.systree.Component):
    """SoftHier cluster control registers and hardware barrier."""

    def __init__(self, parent, name, config: ClusterCsrConfig):
        super().__init__(parent, name, config=config)

        self.add_sources(['pulp/chips/softhier_v2/common/cluster_csr.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=IoV2Sync())

    def i_BARRIER_REQ(self, core: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'barrier_req_{core}', signature='wire<bool>')

    def o_BARRIER_ACK(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('barrier_ack', itf, signature='wire<bool>')
