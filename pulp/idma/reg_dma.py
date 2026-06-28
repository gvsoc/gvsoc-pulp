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
from pulp.idma.reg_dma_config import RegDmaConfig

class RegDma(gvsoc.systree.Component):
    """
    Register-based DMA

    This can be instantiated to handle memory transfers with memory-mapped registers

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    config: RegDmaConfig
        iDMA configuration.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, config: RegDmaConfig,
    ):

        super().__init__(parent, name)

        self.add_sources([
            'pulp/idma/reg_dma.cpp',
            'pulp/idma/fe/idma_fe_reg.cpp',
            'pulp/idma/me/idma_me_2d.cpp',
            'pulp/idma/be/idma_be.cpp',
            'pulp/idma/be/idma_be_axi.cpp',
            'pulp/idma/be/idma_be_tcdm.cpp',
        ])

        self.add_properties({
            "transfer_queue_size": config.transfer_queue_size,
            "burst_queue_size": config.burst_queue_size,
            "burst_size" : config.burst_size,
            "loc_base": config.loc_base,
            "loc_size": config.loc_size,
            "tcdm_width": config.tcdm_width,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        """Returns the input port.

        Incoming requests to the registers to be handled by the DMA should be sent to this
        port.\n
        It instantiates a port of type vp::IoSlave.\n

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_AXI(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI port.

        This port is used for sending burst requests to the AXI interconnect.\n

        Parameters
        ----------
        slave: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind('axi_read', itf, signature='io')
        self.itf_bind('axi_write', itf, signature='io')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        """Binds the TCDM port.

        This port is used for sending line requests to the TCDM memory.\n

        Parameters
        ----------
        slave: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind('tcdm_read', itf, signature='io')
        self.itf_bind('tcdm_write', itf, signature='io')

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        """Binds the IRQ port.

        This port is used to send an interrupt whenever a transfer is finished. This allows
        a processor to sleep until the interrupt is received. The processor should then
        handle all terminated transfers.
        \n


        Parameters
        ----------
        slave: gvsoc.systree.SlaveItf
            Slave interface
        """
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
