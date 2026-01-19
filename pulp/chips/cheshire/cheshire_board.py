#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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

import gvsoc.systree as st
from vp.clock_domain import Clock_domain
from pulp.chips.cheshire.soc import Soc

class Cheshire_board(st.Component):

    def __init__(self, parent, name, parser, options):
        super(Cheshire_board, self).__init__(parent, name, options=options)

        # Soc clock domain
        soc_clock = Clock_domain(self, 'soc_clock_domain', frequency=10000000)

        # SoC
        soc = Soc(self, 'soc', parser, chip=self)

        # Bindings
        self.bind(soc_clock, 'out', soc, 'clock')
        