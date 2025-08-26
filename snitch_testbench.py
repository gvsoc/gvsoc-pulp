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

import pulp.snitch.snitch_core as iss
import memory.memory as memory
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from elftools.elf.elffile import *
import gvsoc.runner
from gvrun.target import TargetProperty, ArchProperty


class SnitchTestbench(st.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)

        TargetProperty(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        memory_size = ArchProperty(
            self, name='memory_size', value=0x10000, description='Memory size',
            cast=int, dump_format='0x%x'
        ).get_value()

        mem = memory.Memory(self, 'imem', size=memory_size)

        ico = router.Router(self, 'ico')
        host = iss.SnitchFast(self, f'core', isa='rv32imfdca')
        loader = utils.loader.loader.ElfLoader(self, 'loader')

        ico.o_MAP(mem.i_INPUT(), base=0x80000000, size=0x10000000)

        loader.o_OUT(ico.i_INPUT())
        loader.o_START(host.i_FETCHEN())
        loader.o_ENTRY(host.i_ENTRY())
        host.o_DATA(ico.i_INPUT())
        host.o_FETCH(ico.i_INPUT())

        # Make sure the loader is notified by any executable attached to the hieararchy of this
        # component so that it is automatically loaded
        self.loader = loader
        self.register_binary_handler(self.handle_binary)

    def configure(self):
        # We configure the loader binary now int he configure steps since it is coming from
        # a target property which can be set either from command line or from the build process
        self.loader.set_binary(self.get_runner_property('binary'))

    def handle_binary(self, binary):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_runner_property('binary', binary)


class SnitchTestbenchWrapper(st.Component):

    def __init__(self, parent, name='snitch.testbench'):

        super(SnitchTestbenchWrapper, self).__init__(parent, name, target_name='snitch.testbench')

        clock = Clock_domain(self, 'clock', frequency=10000000)

        soc = SnitchTestbench(self, 'soc')

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.runner.Target):

    gapy_description = "Snitch testbench"
    model = SnitchTestbenchWrapper
    name = "snitch_testbench"
