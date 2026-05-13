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

from typing_extensions import override
import gvsoc.systree
from gvsoc.gui import Signal, DisplayPulse, DisplayLogicBox
from pulp.idma_v2.reg_dma_config import RegDmaConfig

class RegDmaV2(gvsoc.systree.Component):
    """
    Register-based DMA (IO v2)

    Same shape as RegDma, but the IO ports speak the v2 IO protocol
    (vp/itf/io_v2.hpp) so this generator must be wired against v2 routers
    and v2 memories.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, config: RegDmaConfig,
    ):

        super().__init__(parent, name)

        self.add_sources([
            'pulp/idma_v2/reg_dma.cpp',
            'pulp/idma_v2/fe/idma_fe_reg.cpp',
            'pulp/idma_v2/me/idma_me_2d.cpp',
            'pulp/idma_v2/be/idma_be.cpp',
            'pulp/idma_v2/be/idma_be_axi.cpp',
            'pulp/idma_v2/be/idma_be_tcdm.cpp',
        ])

        self.add_properties({
            "transfer_queue_size": config.transfer_queue_size,
            "burst_queue_size": config.burst_queue_size,
            "burst_size" : config.burst_size,
            "loc_base": config.loc_base,
            "loc_size": config.loc_size,
            "tcdm_width": config.tcdm_width,
            "axi_width": config.axi_width,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io_v2')

    def o_AXI(self, itf: gvsoc.systree.SlaveItf):
        # v2 IoSlave can only be bound to ONE master, so the v1-style
        # double-bind would silently overwrite the read master's resp_meth
        # with the write's. Use o_AXI_READ / o_AXI_WRITE on separate router
        # inputs. The single-itf form below is kept for sync-only routers
        # (router_v2_bandwidth, router_v2_untimed) that never call resp(),
        # where the overwrite is harmless.
        self.itf_bind('axi_read', itf, signature='io_v2')
        self.itf_bind('axi_write', itf, signature='io_v2')

    def o_AXI_READ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('axi_read', itf, signature='io_v2')

    def o_AXI_WRITE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('axi_write', itf, signature='io_v2')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        # Same caveat as o_AXI — see o_TCDM_READ / o_TCDM_WRITE.
        self.itf_bind('tcdm_read', itf, signature='io_v2')
        self.itf_bind('tcdm_write', itf, signature='io_v2')

    def o_TCDM_READ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('tcdm_read', itf, signature='io_v2')

    def o_TCDM_WRITE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('tcdm_write', itf, signature='io_v2')

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq', itf, signature='wire<bool>')

    @override
    def gen_gui(self, parent_signal: Signal):
        active = Signal(self, parent_signal, name=self.name, path='fe/busy', groups='regmap',
            display=DisplayLogicBox('ACTIVE'))

        # This shows details about all the transfers which are queued to the DMA
        queue = Signal(self, active, name='queue')
        _ = Signal(self, queue, name='source', path='fe/src', groups='regmap')
        _ = Signal(self, queue, name='dest', path='fe/dst', groups='regmap')
        _ = Signal(self, queue, name='length', path='fe/length', groups='regmap')
        _ = Signal(self, queue, name='src_stride', path='fe/src_stride', groups='regmap')
        _ = Signal(self, queue, name='dst_stride', path='fe/dst_stride', groups='regmap')
        _ = Signal(self, queue, name='reps', path='fe/reps', groups='regmap')
        _ = Signal(self, queue, name='id', path='fe/id', groups='regmap',
            display=DisplayPulse())
