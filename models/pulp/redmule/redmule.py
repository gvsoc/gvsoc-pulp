import gsystree as st

class RedMule(st.Component):

    def __init__(self, parent, name):

        super(RedMule, self).__init__(parent, name)

        self.set_component('pulp.redmule.redmule')

    def gen_gtkw(self, tree, traces):
        if tree.get_view() == 'overview':
            map_file = tree.new_map_file(self, 'state')
            map_file.add_value(1, 'CadetBlue', 'ACTIVE')

            tree.add_trace(self, self.name, 'ne16_busy', '[7:0]', map_file=map_file, tag='overview')

        else:
            map_file = tree.new_map_file(self, 'core_state', width=32)
            map_file.add_value(0, 'black', 'IDLE')
            map_file.add_value(1, 'CadetBlue', 'START')
            map_file.add_value(2, 'CadetBlue', 'START_STREAMIN')
            map_file.add_value(3, 'CadetBlue', 'STREAMIN_LOAD')
            map_file.add_value(4, 'CadetBlue', 'LOAD_MATRIXVEC')
            map_file.add_value(5, 'CadetBlue', 'STREAMIN')
            map_file.add_value(6, 'CadetBlue', 'LOAD')
            map_file.add_value(7, 'CadetBlue', 'MATRIXVEC')
            map_file.add_value(8, 'CadetBlue', 'NORMQUANT_SHIFT')
            map_file.add_value(9, 'CadetBlue', 'NORMQUANT_MULT')
            map_file.add_value(10, 'CadetBlue', 'NORMQUANT_BIAS')
            map_file.add_value(11, 'CadetBlue', 'STREAMOUT')
            map_file.add_value(12, 'CadetBlue', 'END')

            tree.add_trace(self, self.name, 'fsm_state', '[31:0]', map_file=map_file, tag='overview')

    def gen_gtkw_conf(self, tree, traces):
        if tree.get_view() == 'overview':
            self.vcd_group(self, skip=True)
        else:
            self.vcd_group(self, skip=False)

