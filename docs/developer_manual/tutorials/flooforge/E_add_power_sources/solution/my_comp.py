
import gvsoc.systree

class MyComp(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str, value: int):

        super().__init__(parent, name)

        self.add_sources(['my_comp.cpp'])

        self.add_properties({
            "background_power": {
                "dynamic": {
                    "type": "linear",
                    "unit": "W",
                    "values": {
                        "25": {
                            "1.2": {
                                "any": 0.00050
                            }
                        }
                    }
                },
                "leakage": {
                    "type": "linear",
                    "unit": "W",

                    "values": {
                        "25": {
                            "1.2": {
                                "any": 0.00010
                            }
                        }
                    }
                },
            },
            "access_power": {
                "dynamic": {
                    "type": "linear",
                    "unit": "pJ",

                    "values": {
                        "25": {
                            "1.2": {
                                "any": 10.00000
                            }
                        }
                    }
                }
            }
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')



    def gen_gtkw(self, tree, comp_traces):

        if tree.get_view() == 'overview':
            tree.add_trace(self, self.name, vcd_signal='status[31:0]', tag='overview')
