#
# Copyright (C) 2025 ETH Zurich, University of Bologna and Fondazione ChipsIT
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

import gvsoc.systree
import gvsoc.runner

from pulp.chips.magia_base.magia_soc import MagiaSoc

class MagiaBoard(gvsoc.systree.Component):
    def __init__(self, parent, name:str, parser, options):
        super().__init__(parent, name, options=options)

        [args, __] = parser.parse_known_args()
        binary = args.binary

        # Soc model
        soc = MagiaSoc(self, 'magia-soc', parser, binary)


class Target(gvsoc.runner.Target):
    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
              model=MagiaBoard, description="Magia base test board")
