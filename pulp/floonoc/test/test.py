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

import gvsoc.systree
import gvsoc.runner

import vp.clock_domain
import pulp.floonoc.floonoc
import interco.traffic.generator
import interco.traffic.receiver


GAPY_TARGET = True

class FloonocTest(gvsoc.systree.Component):

    def __init__(self, parent, name, nb_cluster_x, nb_cluster_y, cluster_base, cluster_size, nb_cluster_z=1):
        super().__init__(parent, name)

        self.add_property('nb_cluster_x', nb_cluster_x)
        self.add_property('nb_cluster_y', nb_cluster_y)
        self.add_property('nb_cluster_z', nb_cluster_z)
        self.add_property('cluster_base', cluster_base)
        self.add_property('cluster_size', cluster_size)

        self.add_sources(['test.cpp'])
        self.add_sources(['test0.cpp'])
        self.add_sources(['test1.cpp'])
        self.add_sources(['test2.cpp'])
        self.add_sources(['test3d.cpp'])

    def o_NOC_NI(self, x, y, itf: gvsoc.systree.SlaveItf, z=0):
        self.itf_bind(f'noc_ni_{x}_{y}_{z}', itf, signature='io')

    def o_GENERATOR_CONTROL(self, x, y, itf: gvsoc.systree.SlaveItf, z=0):
        self.itf_bind(f'generator_control_{x}_{y}_{z}', itf, signature='wire<TrafficGeneratorConfig>')

    def o_RECEIVER_CONTROL(self, x, y, itf: gvsoc.systree.SlaveItf, z=0):
        self.itf_bind(f'receiver_control_{x}_{y}_{z}', itf, signature='wire<TrafficReceiverConfig>')

    def i_CLUSTER(self, x, y, z=0) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'cluster_{x}_{y}_{z}', signature='io')

class Testbench(gvsoc.systree.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        nb_cluster_x = 3
        nb_cluster_y = 3
        nb_cluster_z = 3
        cluster_base = 0x80000000
        cluster_size = 0x01000000

        noc = pulp.floonoc.floonoc.FlooNocClusterGridNarrowWide(self, 'noc', 8, 8, nb_cluster_x, nb_cluster_y, nb_z_clusters=nb_cluster_z)

        test = FloonocTest(self, 'test', nb_cluster_x, nb_cluster_y, cluster_base, cluster_size, nb_cluster_z=nb_cluster_z)

        for x in range(0, nb_cluster_x):
            for y in range(0, nb_cluster_y):
                for z in range(0, nb_cluster_z):
                    generator = interco.traffic.generator.Generator(self, f'generator_{x}_{y}_{z}')
                    generator.o_OUTPUT(noc.i_CLUSTER_WIDE_INPUT(x, y, z=z))

                    receiver = interco.traffic.receiver.Receiver(self, f'receiver_{x}_{y}_{z}')

                    test.o_NOC_NI(x, y, noc.i_CLUSTER_WIDE_INPUT(x, y, z=z))
                    test.o_GENERATOR_CONTROL(x, y, generator.i_CONTROL(), z=z)
                    test.o_RECEIVER_CONTROL(x, y, receiver.i_CONTROL(), z=z)

                    noc.o_WIDE_MAP(
                        receiver.i_INPUT(),
                        cluster_base + cluster_size * (z * nb_cluster_y * nb_cluster_x + y * nb_cluster_x + x),
                        cluster_size, x+1, y+1,
                        name=f'ni_{x}_{y}_{z}', z=z)


# This is a wrapping component of the real one in order to connect a clock generator to it
# so that it automatically propagate to other components
class Chip(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):

        super().__init__(parent, name, options=options)

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=100000000)
        soc = Testbench(self, 'soc', parser)
        clock.o_CLOCK    (soc.i_CLOCK    ())




# This is the top target that gapy will instantiate
class Target(gvsoc.runner.Target):

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=Chip, description="RV64 virtual board")
