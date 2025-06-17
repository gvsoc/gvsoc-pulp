import gvsoc.systree

from vp.clock_domain import Clock_domain
import utils.loader.loader
from pulp.padframe.padframe_v1 import Padframe
from utils.clock_generator import Clock_generator

from pulp.chips.chimera.safety_island import SafetyIsland

class ChimeraBoard(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, options):

        super(ChimeraBoard, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=50000000)

        safety_island = SafetyIsland(self, 'safety_island', parser)

        padframe_config_file = 'pulp/chips/chimera/padframe.json'

        padframe = Padframe(self, 'padframe', config_file=padframe_config_file)

        ref_clock = Clock_domain(self, 'ref_clock', frequency=65536)
        ref_clock_generator = Clock_generator(self, 'ref_clock_generator')
        self.bind(ref_clock, 'out', ref_clock_generator, 'clock')

        soc_clock = Clock_domain(self, 'soc_clock_domain', frequency=50000000)
        self.bind(soc_clock, 'out', safety_island, 'clock')
        self.bind(soc_clock, 'out', padframe, 'clock')
        self.bind(safety_island, 'fll_soc_clock', self, 'clock_in')
        self.bind(ref_clock_generator, 'clock_sync', padframe, 'ref_clock_pad')

        self.bind(padframe, 'ref_clock', safety_island, 'ref_clock')