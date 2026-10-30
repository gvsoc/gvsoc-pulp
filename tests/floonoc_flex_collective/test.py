#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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

# System for floonoc_flex's collective (multicast/reduction) correctness
# and benchmarking tests.
# CollectiveTestbench's expected interface (see test.cpp):
#   CollectiveTestbench(parent, name, dim, node_size, node_base, scenario)
#   o_GENERATOR_CONTROL(node: int, is_wide: bool, itf: SlaveItf)
#     node is the flat index y * dim + x (0..dim*dim-1).

import os

import gvsoc.systree
import gvsoc.runner

import vp.clock_domain
from pulp.floonoc_flex.floonoc_flex import FlooNocFlex
from interco.traffic.generator import Generator
from memory.memory import Memory
from gvrun.parameter import TargetParameter

NARROW_WIDTH = 8
WIDE_WIDTH = 64
MESH_DIM = 4
NODE_SIZE = 0x10000
MEM_BASE = 0x2000_0000

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))


def _ni_name(x, y):
    return f"cluster_ni_{x}_{y}"


# Python-side wrapper for the C++ driver (test.cpp), which implements the
# actual test scenarios - see COLLECTIVES.md, "Testing & benchmarking plan".
class CollectiveTestbench(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str, dim: int,
            node_size: int, node_base: int, scenario: str):
        super().__init__(parent, name)

        self.add_property('dim', dim)
        self.add_property('node_size', node_size)
        self.add_property('node_base', node_base)
        self.add_property('scenario', scenario)

        self.add_sources(['test.cpp', 'cpu/iss/flexfloat/flexfloat.c'])

    def o_GENERATOR_CONTROL(self, node: int, is_wide: bool, itf: gvsoc.systree.SlaveItf):
        suffix = 'wide' if is_wide else 'narrow'
        self.itf_bind(f'generator_control_{node}_{suffix}', itf,
            signature='wire<TrafficGeneratorConfig>')


class Soc(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str, scenario: str):
        super().__init__(parent, name)

        collective_enable = scenario != 'unicast_off'
        topology_file = 'mesh_4x4_unicast.yml' if not collective_enable \
            else 'mesh_4x4_collective.yml'
        noc = FlooNocFlex(self, 'noc', narrow_width=NARROW_WIDTH, wide_width=WIDE_WIDTH,
            ni_outstanding_reqs=32, router_input_queue_size=2,
            network_path=os.path.join(_THIS_DIR, 'floogen', topology_file))

        testbench = CollectiveTestbench(self, 'test', dim=MESH_DIM, node_size=NODE_SIZE,
            node_base=MEM_BASE, scenario=scenario)

        for y in range(MESH_DIM):
            for x in range(MESH_DIM):
                node_id = noc.id_map[_ni_name(x, y)]
                node = y * MESH_DIM + x

                # init=False: deterministic all-zero tiles
                mem = Memory(self, f'mem_{x}_{y}', size=NODE_SIZE, init=False)
                base = MEM_BASE + node * NODE_SIZE
                noc.o_NARROW_MAP(mem.i_INPUT(), base, NODE_SIZE, node_id, rm_base=True)
                noc.o_WIDE_BIND(mem.i_INPUT(), node_id)

                narrow_gen = Generator(self, f'narrow_generator_{x}_{y}', nb_pending_reqs=32,
                    collective_enable=collective_enable)
                narrow_gen.o_OUTPUT(noc.i_NARROW_INPUT(node_id))
                testbench.o_GENERATOR_CONTROL(node, False, narrow_gen.i_CONTROL())

                wide_gen = Generator(self, f'wide_generator_{x}_{y}', nb_pending_reqs=32,
                    collective_enable=collective_enable)
                wide_gen.o_OUTPUT(noc.i_WIDE_INPUT(node_id))
                testbench.o_GENERATOR_CONTROL(node, True, wide_gen.i_CONTROL())


# Wraps the SoC with its clock generator, like a normal gvsoc chip.
class Chip(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str = None):
        super().__init__(parent, name)

        scenario = TargetParameter(
            self, name='scenario', value='unicast_on',
            description='Which test to run',
            cast=str
        ).get_value()

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=100000000)
        soc = Soc(self, 'soc', scenario=scenario)
        clock.o_CLOCK(soc.i_CLOCK())


# This is the top target that gapy will instantiate.
class Target(gvsoc.runner.Target):
    gapy_description = "floonoc_flex collective (multicast/reduction) correctness and benchmark tests"
    model = Chip
    name = "test"
