# Copyright (C) 2025 Fondazione Chips-IT

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
import gvsoc.systree
import gvsoc.runner
import os
from pulp.chips.magia_v3.arch import MagiaTree, MagiaArch

from pulp.chips.magia_v3.soc import MagiaV3Soc
from gvrun.parameter import TargetParameter

class MagiaV3Board(gvsoc.systree.Component):
    def __init__(self, parent, name:str, parser, options):
        super().__init__(parent, name, options=options)

        # This declares a target parameter which can be set on the command-line using the
        # --param option when using gvrun script
        TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        # When using gvsoc script, keep getting the binary using the --binary option
        binary = None
        if os.environ.get('USE_GVRUN') is None:
            [args, __] = parser.parse_known_args()
            binary = args.binary

        # Soc model
        tree = MagiaTree(self, 'magia_v3')
        self.soc = MagiaV3Soc(self, 'magia-v3-soc', tree, parser, binary)

    def configure(self):
        # We configure the loader binary now in the configure steps since it is coming from
        # a parameter which can be set either from command line or from the build process
        binary = self.get_parameter('binary')
        if binary is not None and not MagiaArch.ENABLE_PCIE_VFIO:
            self.soc.ctrl_core_loader.set_binary(binary)

    def handle_binary(self, binary):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)


class Target(gvsoc.runner.Target):
    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
              model=MagiaV3Board, description="Magia v3 (memory mapped with pulp cluster) board")