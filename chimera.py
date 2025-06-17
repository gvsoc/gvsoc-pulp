import gvsoc.runner
import utils.loader.loader

from pulp.chips.chimera.chimera_board import ChimeraBoard


class Target(gvsoc.runner.Target):

    gapy_description = "Chimera virtual board"

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options, model=ChimeraBoard)
