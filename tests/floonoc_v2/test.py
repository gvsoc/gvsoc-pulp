#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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

import math

import gvsoc.systree
import gvsoc.runner

import vp.clock_domain
import pulp.floonoc_v2.floonoc_v2
from pulp.floonoc_v2.floonoc_v2 import FlooNocV2Direction
from interco.traffic.generator_v2 import GeneratorV2
from interco.traffic.receiver_v2 import ReceiverV2
from memory.memory_v3 import Memory as MemoryV3, MemoryV3Config
from gvrun.parameter import TargetParameter


class FloonocV2Test(gvsoc.systree.Component):
    """Test driver for the v2 FlooNoC model. Mirrors the v1 FloonocTest layout."""

    def __init__(self, parent, name, nb_cluster_x, nb_cluster_y, cluster_base, cluster_size, use_memory, mem_bw):
        super().__init__(parent, name)

        self.add_property('nb_cluster_x', nb_cluster_x)
        self.add_property('nb_cluster_y', nb_cluster_y)
        self.add_property('cluster_base', cluster_base)
        self.add_property('cluster_size', cluster_size)
        self.add_property('use_memory', use_memory)
        self.add_property('mem_bw', mem_bw)

        self.add_sources(['test.cpp'])
        self.add_sources(['test0.cpp'])

    def o_NOC_NI(self, x, y, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'noc_ni_{x}_{y}', itf, signature='io_v2')

    def o_GENERATOR_CONTROL(self, x, y, is_wide: bool, itf: gvsoc.systree.SlaveItf):
        is_wide_str = 'w' if is_wide else 'n'
        self.itf_bind(f'generator_control_{x}_{y}_{is_wide_str}', itf, signature='wire<TrafficGeneratorConfig>')

    def o_RECEIVER_CONTROL(self, x, y, is_wide: bool, itf: gvsoc.systree.SlaveItf):
        is_wide_str = 'w' if is_wide else 'n'
        self.itf_bind(f'receiver_control_{x}_{y}_{is_wide_str}', itf, signature='wire<TrafficReceiverConfig>')

    def i_CLUSTER(self, x, y) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'cluster_{x}_{y}', signature='io_v2')


class Testbench(gvsoc.systree.Component):

    def __init__(self, parent, name, use_memory=False, target_bw=0, mem_bw=0):
        super().__init__(parent, name)

        nb_cluster_x = 3
        nb_cluster_y = 3
        cluster_base = 0x80000000
        cluster_size = 0x010_0000
        mem_base = 0x9000_0000
        mem_size = 0x10_0000
        mem_group_size = 0x1000_0000

        noc = pulp.floonoc_v2.floonoc_v2.FlooNocV2ClusterGridNarrowWide(self, 'noc', 64, 8, nb_cluster_x, nb_cluster_y, ni_outstanding_reqs=32)

        test = FloonocV2Test(self, 'test', nb_cluster_x, nb_cluster_y, cluster_base, cluster_size, use_memory, mem_bw)

        for x in range(0, nb_cluster_x):
            for y in range(0, nb_cluster_y):
                generator_w = GeneratorV2(self, f'generator_{x}_{y}_w')
                generator_w.o_OUTPUT(noc.i_CLUSTER_WIDE_INPUT(x, y))
                generator_n = GeneratorV2(self, f'generator_{x}_{y}_n')
                generator_n.o_OUTPUT(noc.i_CLUSTER_NARROW_INPUT(x, y))

                if use_memory:
                    width_log2 = -1 if mem_bw == 0 else int(math.log2(mem_bw))
                    receiver_w = MemoryV3(self, f'mem_{x}_{y}_w',
                        config=MemoryV3Config(size=mem_size, width_log2=width_log2))
                    receiver_n = MemoryV3(self, f'mem_{x}_{y}_n',
                        config=MemoryV3Config(size=mem_size, width_log2=width_log2))
                else:
                    receiver_w = ReceiverV2(self, f'receiver_{x}_{y}_w', mem_size=1 << 20)
                    receiver_n = ReceiverV2(self, f'receiver_{x}_{y}_n', mem_size=1 << 20)
                    test.o_RECEIVER_CONTROL(x+1, y+1, True, receiver_w.i_CONTROL())
                    test.o_RECEIVER_CONTROL(x+1, y+1, False, receiver_n.i_CONTROL())

                test.o_NOC_NI(x, y, noc.i_CLUSTER_WIDE_INPUT(x, y))
                test.o_GENERATOR_CONTROL(x, y, True, generator_w.i_CONTROL())
                test.o_GENERATOR_CONTROL(x, y, False, generator_n.i_CONTROL())

                noc.o_MAP(cluster_base + cluster_size * (y * nb_cluster_x + x),
                    cluster_size, x+1, y+1, rm_base=True)
                noc.o_WIDE_BIND(receiver_w.i_INPUT(), x+1, y+1)
                noc.o_NARROW_BIND(receiver_n.i_INPUT(), x+1, y+1)

        mem_group_base = mem_base
        for i in range(0, 4):
            target_mem_base = mem_group_base

            nb_targets = nb_cluster_x if i < 2 else nb_cluster_y

            bound_name = ['up', 'down', 'left', 'right'][i]

            if bound_name != 'up':
                for x in range(0, nb_targets):
                    coord = [(x+1, nb_cluster_y+1), (x+1, 0), (0, x+1), (nb_cluster_x+1, x+1)][i]

                    if use_memory:
                        width_log2 = -1 if mem_bw == 0 else int(math.log2(mem_bw))
                        mem_w = MemoryV3(self, f'mem_{bound_name}_{x}_w',
                            config=MemoryV3Config(size=mem_size, width_log2=width_log2))
                        mem_n = MemoryV3(self, f'mem_{bound_name}_{x}_n',
                            config=MemoryV3Config(size=mem_size, width_log2=width_log2))
                    else:
                        mem_w = ReceiverV2(self, f'rcv_{bound_name}_{x}_w', mem_size=1 << 20)
                        mem_n = ReceiverV2(self, f'rcv_{bound_name}_{x}_n', mem_size=1 << 20)
                        test.o_RECEIVER_CONTROL(coord[0], coord[1], True, mem_w.i_CONTROL())
                        test.o_RECEIVER_CONTROL(coord[0], coord[1], False, mem_n.i_CONTROL())
                    noc.o_MAP(target_mem_base, mem_size, coord[0], coord[1], rm_base=True)
                    noc.o_WIDE_BIND(mem_w.i_INPUT(), coord[0], coord[1])
                    noc.o_NARROW_BIND(mem_n.i_INPUT(), coord[0], coord[1])
                    target_mem_base += mem_size

            else:
                if use_memory:
                    width_log2 = -1 if mem_bw == 0 else int(math.log2(mem_bw))
                    mem_w = MemoryV3(self, f'mem_{bound_name}_w',
                        config=MemoryV3Config(size=mem_size, width_log2=width_log2))
                    mem_n = MemoryV3(self, f'mem_{bound_name}__n',
                        config=MemoryV3Config(size=mem_size, width_log2=width_log2))
                else:
                    mem_w = ReceiverV2(self, f'rcv_{bound_name}_w', mem_size=1 << 20)
                    mem_n = ReceiverV2(self, f'rcv_{bound_name}_n', mem_size=1 << 20)
                    for x in range(0, nb_targets):
                        coord = [(x+1, nb_cluster_y+1), (x+1, 0), (0, x+1), (nb_cluster_x+1, x+1)][i]
                        test.o_RECEIVER_CONTROL(coord[0], coord[1], True, mem_w.i_CONTROL())
                        test.o_RECEIVER_CONTROL(coord[0], coord[1], False, mem_n.i_CONTROL())

                for x in range(0, nb_targets):
                    coord = [(x+1, nb_cluster_y+1), (x+1, 0), (0, x+1), (nb_cluster_x+1, x+1)][i]
                    noc.o_WIDE_BIND(mem_w.i_INPUT(), coord[0], coord[1])
                    noc.o_NARROW_BIND(mem_n.i_INPUT(), coord[0], coord[1])

                noc.o_MAP_DIR(mem_group_base, mem_group_size, FlooNocV2Direction.UP,
                    name=f'mem_up', rm_base=True)

            mem_group_base += mem_group_size


class Chip(gvsoc.systree.Component):

    def __init__(self, parent, name=None):

        super().__init__(parent, name)

        use_memory = TargetParameter(
            self, name='use_memory', value=False, description='Use memory as targets',
            cast=bool
        ).get_value()

        mem_bw = TargetParameter(
            self, name='mem_bw', value=64, description='When using memory as targets, specify their bandwidth',
            cast=int
        ).get_value()

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=100000000)
        soc = Testbench(self, 'soc', use_memory=use_memory, mem_bw=mem_bw)
        clock.o_CLOCK(soc.i_CLOCK())


class Target(gvsoc.runner.Target):

    gapy_description = "Floonoc v2 test"
    model = Chip
    name = "test"
