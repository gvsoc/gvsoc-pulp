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

"""io_v2 port of the Snitch cluster peripheral registers.

Same regmap (``snitch_cluster_peripheral_reg.hjson``) and behaviour as the
v1 ``cluster_registers.py``: the CLINT set / clear registers drive the core
interrupts, and the barrier unit releases all the cores once each of them
has read its barrier CSR. The ``input`` port speaks io_v2.
"""

from config_tree import Config, cfg_field
import gvsoc.systree
import gvsoc.signature
import gvsoc.gui
import regmap.regmap
import regmap.regmap_hjson
import regmap.regmap_c_header


class ClusterRegistersConfig(Config):
    nb_cores: int = cfg_field(default=1, dump=True, desc=(
        "Number of cores of the cluster (interrupt lines and barrier inputs)"
    ))


class ClusterRegisters(gvsoc.systree.Component):

    def __init__(self, parent, name, config: ClusterRegistersConfig):
        super().__init__(parent, name, config=config)

        self.add_sources(['pulp/snitch/snitch_cluster/cluster_registers_v2.cpp'])

    def gen(self, builddir, installdir):
        comp_path = 'pulp/snitch/snitch_cluster'
        regmap_path = f'{comp_path}/snitch_cluster_peripheral_reg.hjson'
        regmap_instance = regmap.regmap.Regmap('cluster_periph')
        regmap.regmap_hjson.import_hjson(regmap_instance, self.get_file_path(regmap_path))
        regmap.regmap_c_header.dump_to_header(regmap=regmap_instance, name='cluster_periph',
            header_path=f'{builddir}/{comp_path}/cluster_periph', headers=['regfields', 'gvsoc'])

    def o_EXTERNAL_IRQ(self, core: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'external_irq_{core}', itf, signature='wire<bool>')

    def o_ICACHE_PREFETCH_ENABLE(self, itf: gvsoc.systree.SlaveItf):
        """ICACHE_PREFETCH_ENABLE register (reset 1), to the
        ``i_ENABLE_PREFETCHING`` of every L0."""
        self.itf_bind('icache_prefetch_enable', itf, signature='wire<bool>')

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature=gvsoc.signature.IoV2SingleReq())

    def i_BARRIER_REQ(self, core: int) -> gvsoc.systree.SlaveItf:
        """Barrier notification of core ``core`` (its barrier CSR read)."""
        return gvsoc.systree.SlaveItf(self, f'barrier_req_{core}', signature='wire<bool>')

    def o_BARRIER_ACK(self, itf: gvsoc.systree.SlaveItf):
        """Barrier release, sent to all the cores once they all arrived."""
        self.itf_bind('barrier_ack', itf, signature='wire<bool>')

    def gen_gui(self, parent_signal):
        return gvsoc.gui.Signal(self, parent_signal, name=self.name, is_group=True, groups=["regmap"])
