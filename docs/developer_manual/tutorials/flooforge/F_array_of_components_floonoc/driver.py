import gvsoc.systree


class Driver(gvsoc.systree.Component):
    """Minimal traffic-generator controller.

    Fires a fixed dummy write burst on every connected generator, each
    targeting its own address (`targets[i]`), as soon as the simulation
    comes out of reset, then polls them until they are all done and stops
    the simulation.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str,
            targets: list, transfer_size: int=4096, packet_size: int=64):
        super().__init__(parent, name)

        self.add_sources(['driver.cpp'])

        self.add_properties({
            'targets': targets,
            'transfer_size': transfer_size,
            'packet_size': packet_size,
        })

    def o_GENERATOR(self, index: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'generator_{index}', itf, signature='wire<TrafficGeneratorConfig>')
