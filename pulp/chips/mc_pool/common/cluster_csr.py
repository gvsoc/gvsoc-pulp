#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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

class ClusterCSR(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, wakeup_latency: int=0,
                 cluster_id: int=0, num_cluster_x: int=1, num_cluster_y: int=1,
                 global_barrier_addr=0, global_barrier_stride: int=0,
                 soc_register_base: int=0x90000000):

        super().__init__(parent, name)

        self.add_sources(['pulp/chips/mc_pool/common/cluster_csr.cpp'])

        self.add_properties({
            'wakeup_latency': wakeup_latency,
            'cluster_id': cluster_id,
            'num_cluster_x': num_cluster_x,
            'num_cluster_y': num_cluster_y,
            'global_barrier_addr' : global_barrier_addr,
            'global_barrier_stride' : global_barrier_stride,
            'soc_register_base': soc_register_base,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
    
    def i_BARRIER_REG_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'barrier_reg_input', signature='io')

    def o_BARRIER_ACK(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('barrier_ack', itf, signature='wire<bool>')
    
    def gen_gui(self, parent_signal):
        return gvsoc.gui.Signal(self, parent_signal, name=self.name, is_group=True, groups=["regmap"])

    def i_GLOBAL_BARRIER_SLAVE(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'global_barrier_slave', signature='io')
    
    def o_GLOBAL_BARRIER_MASTER(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('global_barrier_master', itf, signature='io')

    def o_CLUSTER_EOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('cluster_eoc', itf, signature='io')

    def i_HBM_PRELOAD_DONE(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm_preload_done', signature='wire<bool>')

    def i_INST_PREHEAT_DONE(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'inst_preheat_done', signature='wire<bool>')

    def o_FETCH_START(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'fetch_start', itf, signature='wire<bool>')