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
# Authors: Lorenzo Zuolo, Fondazione Chips-IT (lorenzo.zuolo@chips.it)
#

import gvsoc.systree


class PulpOpenDma(gvsoc.systree.Component):
    """
    PULP Open cluster DMA

    iDMA-based cluster DMA for the PULP Open cluster, matching the dmac_wrap that pulp_cluster
    compiles with the idma bender target. It can be instantiated in place of Mchan: it exposes the
    same set of ports, one control port per core plus the peripheral ones, four TCDM ports and one
    external port.

    The defaults mirror the parameters pulp_cluster passes to dmac_wrap in its iDMA branch.

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    nb_cores: int
        Number of cores, each getting its own register file port and completion event.
    nb_pe_ports: int
        Number of peripheral register file ports, on top of the per-core ones.
    nb_streams: int
        Number of streams, each with its own transfer identifier counters.
    global_queue_depth: int
        Number of transfers which can be accepted before the middle-end takes them over.
    transfer_queue_size: int
        Number of transfer requests which can be queued to the middle-end.
    burst_queue_size: int
        Maximum number of outstanding burst requests.
    burst_size: int
        Maximum burst size, in bytes. 0 leaves it unconstrained.
    loc_base: int
        Base address of the local area, used to tell the TCDM backend from the AXI one.
    loc_size: int
        Size of the local area.
    tcdm_width: int
        Width of the local interconnect, in bytes.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str,
            nb_cores: int=8,
            nb_pe_ports: int=2,
            nb_streams: int=2,
            global_queue_depth: int=8,
            transfer_queue_size: int=8,
            burst_queue_size: int=8,
            burst_size: int=0,
            loc_base: int=0,
            loc_size: int=0,
            tcdm_width: int=0):

        super().__init__(parent, name)

        self.add_sources([
            'pulp/idma/pulp_open_dma.cpp',
            'pulp/idma/fe/idma_fe_reg32_3d.cpp',
            'pulp/idma/me/idma_me_3d.cpp',
            'pulp/idma/be/idma_be.cpp',
            'pulp/idma/be/idma_be_axi.cpp',
            'pulp/idma/be/idma_be_tcdm.cpp',
        ])

        self.nb_cores = nb_cores
        self.nb_pe_ports = nb_pe_ports

        self.add_properties({
            "nb_cores": nb_cores,
            # The front-end sees one flat set of register file ports, the per-core ones first
            "nb_ports": nb_cores + nb_pe_ports,
            "nb_streams": nb_streams,
            "global_queue_depth": global_queue_depth,
            "transfer_queue_size": transfer_queue_size,
            "burst_queue_size": burst_queue_size,
            "burst_size": burst_size,
            "loc_base": loc_base,
            "loc_size": loc_size,
            "tcdm_width": tcdm_width,
        })

    def i_CTRL(self, core: int) -> gvsoc.systree.SlaveItf:
        """Returns the control port of one core.

        This is the register file the core programs its transfers through, reachable both at the
        cluster DMA address and through the per-core demux alias.\n

        Parameters
        ----------
        core: int
            Index of the core.

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, f'ctrl_{core}', signature='io')

    def i_CTRL_PE(self, port: int) -> gvsoc.systree.SlaveItf:
        """Returns one of the peripheral control ports.

        These are the register files reachable from the cluster peripheral interconnect, used when
        a transfer is programmed from outside the cores.\n

        Parameters
        ----------
        port: int
            Index of the peripheral port.

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, f'ctrl_{self.nb_cores + port}', signature='io')

    def o_EVENT(self, core: int, itf: gvsoc.systree.SlaveItf):
        """Binds the completion event port of one core.

        Following the hardware, the event is broadcast: every core is notified when any transfer
        completes, whichever port programmed it. No interrupt is generated.\n

        Parameters
        ----------
        core: int
            Index of the core.
        itf: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind(f'event_{core}', itf, signature='wire<bool>')

    def o_EVENT_PE(self, port: int, itf: gvsoc.systree.SlaveItf):
        """Binds the completion event port of one peripheral port.

        Parameters
        ----------
        port: int
            Index of the peripheral port.
        itf: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind(f'event_pe_{port}', itf, signature='wire<bool>')

    def o_AXI(self, itf: gvsoc.systree.SlaveItf):
        """Binds the AXI port.

        This port is used for sending burst requests to the cluster interconnect.\n

        Parameters
        ----------
        itf: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind('axi_read', itf, signature='io')
        self.itf_bind('axi_write', itf, signature='io')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        """Binds the TCDM port.

        This port is used for sending line requests to the TCDM memory.\n

        Parameters
        ----------
        itf: gvsoc.systree.SlaveItf
            Slave interface
        """
        self.itf_bind('tcdm_read', itf, signature='io')
        self.itf_bind('tcdm_write', itf, signature='io')
