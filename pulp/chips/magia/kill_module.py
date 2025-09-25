import gvsoc.systree

class KillModule(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                kill_addr_base: int,
                kill_addr_size: int,
                nb_cores_to_wait: int):

        super().__init__(parent, name)

        self.add_properties({
            'kill_addr_base' : kill_addr_base,
            'kill_addr_size' : kill_addr_size,
            'nb_cores_to_wait' : nb_cores_to_wait,
        })

        self.add_sources(['pulp/chips/magia/kill_module.cpp'])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')