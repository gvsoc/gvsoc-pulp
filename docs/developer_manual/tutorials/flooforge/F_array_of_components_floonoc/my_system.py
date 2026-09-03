import gvsoc.systree
import gvsoc.runner

import vp.clock_domain
import pulp.floonoc.floonoc
import interco.traffic.generator
from memory.memory import Memory

import driver

# 2x2 array of "clusters", each one just a dummy traffic generator, connected
# through a FlooNoc 2D mesh. The mesh grid is (NB_CLUSTER_X+2) x (NB_CLUSTER_Y+2):
# one row/column of border nodes is added all around for external targets.
NB_CLUSTER_X = 2
NB_CLUSTER_Y = 2

MEM_BASE = 0x9000_0000
MEM_SIZE = 0x10_0000


class Soc(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str):
        super().__init__(parent, name)

        # narrow_width and wide_width are given in bytes routed per cycle on the
        # narrow and wide channels of the mesh. This tutorial only uses the
        # narrow channel.
        noc = pulp.floonoc.floonoc.FlooNocClusterGridNarrowWide(
            self, 'noc', wide_width=8, narrow_width=8,
            nb_x_clusters=NB_CLUSTER_X, nb_y_clusters=NB_CLUSTER_Y,
            ni_outstanding_reqs=32)

        # A single memory, reachable by every generator, sitting on a border node
        # of the mesh (x=0 is the left border, so (0, 1) is not a grid corner).
        mem = Memory(self, 'mem', size=MEM_SIZE)
        noc.o_NARROW_MAP(mem.i_INPUT(), MEM_BASE, MEM_SIZE, 0, 1, rm_base=True)

        nb_generators = NB_CLUSTER_X * NB_CLUSTER_Y
        drv = driver.Driver(self, 'driver', nb_generators=nb_generators,
            target_base=MEM_BASE, transfer_size=4096, packet_size=64)

        index = 0
        for x in range(NB_CLUSTER_X):
            for y in range(NB_CLUSTER_Y):
                generator = interco.traffic.generator.Generator(self, f'generator_{x}_{y}')
                generator.o_OUTPUT(noc.i_CLUSTER_NARROW_INPUT(x, y))
                drv.o_GENERATOR(index, generator.i_CONTROL())
                index += 1


# Wraps the SoC with its clock generator, like a normal gvsoc chip
class Chip(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str=None):
        super().__init__(parent, name)

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=100000000)
        soc = Soc(self, 'soc')
        clock.o_CLOCK(soc.i_CLOCK())


# This is the top target that gapy will instantiate
class Target(gvsoc.runner.Target):
    gapy_description = "Array of dummy traffic generators connected through a FlooNoc mesh"
    model = Chip
    name = "my_system"
