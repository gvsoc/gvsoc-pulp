import gvsoc.systree as st
import gvsoc

class RedMule(st.Component):

    def __init__(self, parent, name):

        super(RedMule, self).__init__(parent, name)

        self.set_component('pulp.redmule.redmule')


    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_OUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('out', itf, signature='io')

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('irq', itf, signature='wire<bool>')
