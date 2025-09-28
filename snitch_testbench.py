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
from gvrun.parameter import TargetParameter
from gvrun.attribute import Tree, Area, Value



# class SnitchCore(Tree):
#     def __init__(self, parent, name, id):
#         super().__init__(parent, name)
#         self.isa    = Value(self, 'isa', 'rv32imfdca', description='ISA string of the core')
#         self.id    = Value(self, 'id', id, cast=int)

# class SnitchCluster(Tree):
#     def __init__(self, parent, name):
#         super().__init__(parent, name)

#         self.nb_cores = Value(self, 'nb_cores', 10, cast=int)
#         self.cores = []
#         self.cores.append(SnitchCore(self, f'core_{len(self.cores)}', len(self.cores)))
#         self.cores.append(SnitchCore(self, f'core_{len(self.cores)}', len(self.cores)))
#         self.cores.append(SnitchCore(self, f'core_{len(self.cores)}', len(self.cores)))
#         self.cores.append(SnitchCore(self, f'core_{len(self.cores)}', len(self.cores)))

# class SnitchTestbenchAttr(Tree):

#     def __init__(self, parent):
#         super().__init__(parent)
#         self.mem_l0 = Area(self, 'mem_l0', 0x80000000, 0x100000, description='Address range of the memory')
#         self.mem_l1 = Area(self, 'mem_l1', 0x90000000, 0x100000, description='Address range of the memory')
#         self.mem_l2 = Area(self, 'mem_l2', 0xA0000000, 0x100000, description='Address range of the memory')
#         self.isa    = Value(self, 'isa', 'rv32imfdca')
#         self.cluster0 = SnitchCluster(self, 'cluster_0')
#         self.cluster1 = SnitchCluster(self, 'cluster_1')

class SnitchTestbenchAttr(Tree):

    def __init__(self, parent):
        super().__init__(parent)
        self.mem_l0 = Area(self, 'mem_l0', 0x8000_0000, 0x10_0000, description='Address range of the memory')
        self.mem_l1 = Area(self, 'mem_l1', 0x8010_0000, 0x10_0000, description='Address range of the memory')
        self.mem_l2 = Area(self, 'mem_l2', 0x8020_0000, 0x100000, description='Address range of the memory')
        self.mem_l0_async = Area(self, 'mem_l0', 0x9000_0000, 0x10_0000, description='Address range of the memory')
        self.mem_l1_async = Area(self, 'mem_l1', 0x9010_0000, 0x10_0000, description='Address range of the memory')
        self.mem_l2_async = Area(self, 'mem_l2', 0x9020_0000, 0x10_0000, description='Address range of the memory')
        self.async_area = Area(self, 'mem_l2', 0x9000_0000, 0x30_0000, description='Address range of the memory')
        self.mem_l0_async2 = Area(self, 'mem_l0', 0xA000_0000, 0x10_0000, description='Address range of the memory')
        self.mem_l1_async2 = Area(self, 'mem_l1', 0xA010_0000, 0x10_0000, description='Address range of the memory')
        self.mem_l2_async2 = Area(self, 'mem_l2', 0xA020_0000, 0x10_0000, description='Address range of the memory')
        self.async2_area = Area(self, 'mem_l2', 0xA000_0000, 0x30_0000, description='Address range of the memory')
        self.isa    = Value(self, 'isa', 'rv32imfdca')


class SnitchTestbench(st.Component):

    def __init__(self, parent, name, attr):
        super().__init__(parent, name)

        TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        mem_l0 = memory.Memory(self, 'mem_l0', size=attr.mem_l0.size, atomics=True, latency=0)
        mem_l1 = memory.Memory(self, 'mem_l1', size=attr.mem_l1.size, atomics=True, latency=10)
        mem_l2 = memory.Memory(self, 'mem_l2', size=attr.mem_l2.size, atomics=True, latency=1000)

        ico = router.Router(self, 'ico')
        ico_async = router.Router(self, 'ico_async', synchronous=False, max_input_pending_size=4)
        ico_async2 = router.Router(self, 'ico_async2', synchronous=False, max_input_pending_size=4, bandwidth=1)
        host = iss.SnitchFast(self, f'core', isa=attr.isa, nb_outstanding=8)
        loader = utils.loader.loader.ElfLoader(self, 'loader')

        ico.o_MAP(mem_l0.i_INPUT(), base=attr.mem_l0.base, size=attr.mem_l0.size)
        ico.o_MAP(mem_l1.i_INPUT(), base=attr.mem_l1.base, size=attr.mem_l1.size)
        ico.o_MAP(mem_l2.i_INPUT(), base=attr.mem_l2.base, size=attr.mem_l2.size)
        ico.o_MAP(ico_async.i_INPUT(), base=attr.async_area.base, size=attr.async_area.size, rm_base=False)
        ico.o_MAP(ico_async.i_INPUT(), name="async2", base=attr.async2_area.base, size=attr.async2_area.size, rm_base=False)

        ico_async.o_MAP(mem_l0.i_INPUT(), base=attr.mem_l0_async.base, size=attr.mem_l0_async.size, latency=0)
        ico_async.o_MAP(mem_l1.i_INPUT(), base=attr.mem_l1_async.base, size=attr.mem_l1_async.size, latency=10)
        ico_async.o_MAP(mem_l2.i_INPUT(), base=attr.mem_l2_async.base, size=attr.mem_l2_async.size, latency=1000)
        ico_async.o_MAP(ico_async2.i_INPUT(), base=attr.async2_area.base, size=attr.async2_area.size, rm_base=False)

        ico_async2.o_MAP(mem_l0.i_INPUT(), base=attr.mem_l0_async2.base, size=attr.mem_l0_async2.size, latency=0)
        ico_async2.o_MAP(mem_l1.i_INPUT(), base=attr.mem_l1_async2.base, size=attr.mem_l1_async2.size, latency=10)
        ico_async2.o_MAP(mem_l2.i_INPUT(), base=attr.mem_l2_async2.base, size=attr.mem_l2_async2.size, latency=1000)

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
        # a parameter which can be set either from command line or from the build process
        self.loader.set_binary(self.get_parameter('binary'))

    def handle_binary(self, binary):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)


class SnitchTestbenchWrapper(st.Component):

    def __init__(self, parent, name=None):

        super(SnitchTestbenchWrapper, self).__init__(parent, name)

        self.set_target_name('snitch.testbench')

        attr = self.set_attributes(SnitchTestbenchAttr(self))

        clock = Clock_domain(self, 'clock', frequency=10000000)

        soc = SnitchTestbench(self, 'soc', attr)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.runner.Target):

    gapy_description = "Snitch testbench"
    model = SnitchTestbenchWrapper
    name = "snitch_testbench"
