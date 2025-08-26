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
from gvrun.target import TargetProperty


class SnitchTestbench(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        binary = TargetProperty(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        ).value

        binaries = []
        if binary is not None:
            binaries.append(binary)

        mem = memory.Memory(self, 'imem', size=0x10000)

        ico = router.Router(self, 'ico')
        host = iss.SnitchFast(self, f'core', isa='rv32imfdca', binaries=binaries)
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        ico.o_MAP(mem.i_INPUT(), base=0x80000000, size=0x10000000)

        loader.o_OUT(ico.i_INPUT())
        loader.o_START(host.i_FETCHEN())
        loader.o_ENTRY(host.i_ENTRY())
        host.o_DATA(ico.i_INPUT())
        host.o_FETCH(ico.i_INPUT())


class SnitchTestbenchWrapper(st.Component):

    def __init__(self, parent, name, parser, options):

        super(SnitchTestbenchWrapper, self).__init__(parent, name, options=options,
            target_name='snitch.testbench')

        clock = Clock_domain(self, 'clock', frequency=10000000)

        soc = SnitchTestbench(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')

        # Make sure the loader is notified by any executable attached to this component so that it is
        # automatically loaded
        self.add_binary_loader(self.get_component('soc/loader'))


class Target(gvsoc.runner.Target):

    gapy_description="Snitch testbench"

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=SnitchTestbenchWrapper)
