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
from pulp.chips.carfield.soc import Soc

class Carfield_board(st.Component):

    def __init__(self, parent, name, parser, options, soc_config_file='pulp/chips/carfield/soc.json'):
        super(Carfield_board, self).__init__(parent, name, options=options)

        parser.add_argument("--arg", dest="args", action="append",
            help="Specify application argument (passed to main)")
        
        soc_config_file = self.add_property('soc_config_file', soc_config_file)

        # Soc clock domain
        soc_clock = Clock_domain(self, 'soc_clock_domain', frequency=10000000)

        # SoC
        soc = Soc(self, 'soc', parser, config_file=soc_config_file, chip=self)

        # Bindings
        self.bind(soc_clock, 'out', soc, 'clock')
        