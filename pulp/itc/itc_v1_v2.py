# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0

import gvsoc.gui
import gvsoc.systree


class Itc_v1_V2(gvsoc.systree.Component):
    """io_v2 sibling of :class:`pulp.itc.itc_v1.Itc_v1`. Same register map and
    wire ports; the IO slave speaks the v2 protocol."""

    def __init__(self, parent: gvsoc.systree.Component, name: str):

        super().__init__(parent, name)

        self.set_component('pulp.itc.itc_v1_impl_v2')

        self.add_properties({
            "nb_fifo_events": 8,
            "fifo_irq": 26
        })

    def o_IRQ_REQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq_req', itf, signature='wire<int>')

    def i_IRQ_ACK(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, itf_name='irq_ack', signature='wire<int>')

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io_v2')

    def i_EVENT(self, id: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, itf_name=f'in_event_{id}', signature='wire<bool>')

    def gen_gtkw(self, tree, comp_traces):

        if tree.get_view() == 'overview':

            tree.add_trace(self, 'status', 'status', '[31:0]', tag='irq')
            tree.add_trace(self, 'mask', 'mask', '[31:0]', tag='irq')

    def gen_gui(self, parent_signal):
        active = gvsoc.gui.Signal(self, parent_signal, name=self.name, path='irq',
            display=gvsoc.gui.DisplayPulse())

        gvsoc.gui.Signal(self, active, name='status', path='status', groups='regmap')
        gvsoc.gui.Signal(self, active, name='input_status', path='input_status', groups='regmap')
        gvsoc.gui.Signal(self, active, name='mask', path='mask', groups='regmap')
