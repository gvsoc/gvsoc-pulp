import gvsoc.systree as st

class RedMule(st.Component):

    def __init__(self, parent, name):

        super(RedMule, self).__init__(parent, name)

        self.set_component('pulp.redmule.redmule')