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

import gvsoc.gui
import gvsoc.systree as st

class Itc_v1(st.Component):

    def __init__(self, parent, name):

        super(Itc_v1, self).__init__(parent, name)

        self.set_component('pulp.itc.itc_v1_impl')

        self.add_properties({
            "nb_fifo_events": 8,
            "fifo_irq": 26
        })

    def o_IRQ_REQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq_req', itf, signature=f'wire<int>')

    def i_IRQ_ACK(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, itf_name='irq_ack', signature='wire<int>')

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def i_EVENT(self, id: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, itf_name=f'in_event_{id}', signature='wire<bool>')

    def gen_gtkw(self, tree, comp_traces):

        if tree.get_view() == 'overview':

            tree.add_trace(self, 'status', 'status', '[31:0]', tag='irq')
            tree.add_trace(self, 'mask', 'mask', '[31:0]', tag='irq')

    def gen_gui(self, parent_signal):
        active = gvsoc.gui.Signal(self, parent_signal, name=self.name)

        gvsoc.gui.Signal(self, active, name='status', path='status', groups='regmap')
        gvsoc.gui.Signal(self, active, name='input_status', path='input_status', groups='regmap')
        gvsoc.gui.Signal(self, active, name='mask', path='mask', groups='regmap')
