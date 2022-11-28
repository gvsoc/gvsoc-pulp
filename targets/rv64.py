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

import gv.gvsoc_runner
import ips.iss.iss as iss
import ips.memory.memory as memory
from ips.clock.clock_domain import Clock_domain
import ips.interco.router as router
import utils.loader.loader
import gsystree as st

class Soc(st.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)

        mem = memory.Memory(self, 'mem', size=0x1000000)

        ico = router.Router(self, 'ico')

        ico.add_mapping('mem', base=0, remove_offset=0, size=0x1000000)
        self.bind(ico, 'mem', mem, 'input')

        host = iss.Iss(self, 'host', vp_component='pulp.cpu.iss.iss_rv64',
            boot_addr=0x00010000)

        loader = utils.loader.loader.ElfLoader(self, 'loader')

        self.bind(host, 'fetch', ico, 'input')
        self.bind(host, 'data', ico, 'input')
        self.bind(loader, 'out', ico, 'input')
        self.bind(loader, 'start', host, 'fetchen')
        self.bind(loader, 'entry', host, 'bootaddr')



class Target(gv.gvsoc_runner.Runner):

    def __init__(self, options):

        super(Target, self).__init__(parent=None, name='top', options=options)

        clock = Clock_domain(self, 'clock', frequency=50000000)

        soc = Soc(self, 'soc')

        self.bind(clock, 'out', soc, 'clock')


    def __str__(self) -> str:
        return "RV64 virtual board"