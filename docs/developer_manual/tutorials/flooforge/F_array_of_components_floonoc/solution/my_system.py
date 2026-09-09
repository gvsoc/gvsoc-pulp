import gvsoc.systree
import gvsoc.runner

import vp.clock_domain
import pulp.floonoc.floonoc
import interco.traffic.generator
from memory.memory import Memory

import driver

# 3x3 array of hybrid compute+memory "clusters": each one is both a dummy
# traffic generator AND a local memory, connected through a FlooNoc 2D mesh.
# Each generator targets its point-symmetric mirror tile's memory. 
#  The mesh grid is (NB_CLUSTER_X+2) x (NB_CLUSTER_Y+2): 
# one row/column of border nodes is added all around,
# though this tutorial does not use them.
NB_CLUSTER_X = 3
NB_CLUSTER_Y = 3

MEM_BASE = 0x9000_0000
TILE_MEM_SIZE = 0x10_0000

TRANSFER_SIZE = 4096*64
PACKET_SIZE = 64


def _mirror(x, y):
    return (NB_CLUSTER_X - 1 - x, NB_CLUSTER_Y - 1 - y)


def _tile_base(x, y):
    tile_id = y * NB_CLUSTER_X + x
    return MEM_BASE + tile_id * TILE_MEM_SIZE


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

        # One local memory per cluster, mapped directly onto that cluster's
        # own mesh node - the same node used to inject its generator's
        # traffic (raw grid position (x+1, y+1), since node (0, 0) is a
        # border corner).
        for x in range(NB_CLUSTER_X):
            for y in range(NB_CLUSTER_Y):
                mem = Memory(self, f'mem_{x}_{y}', size=TILE_MEM_SIZE)
                noc.o_NARROW_MAP(mem.i_INPUT(), _tile_base(x, y), TILE_MEM_SIZE, x + 1, y + 1, rm_base=True)

        # Each generator's target is its mirror tile's memory address.
        targets = []
        for x in range(NB_CLUSTER_X):
            for y in range(NB_CLUSTER_Y):
                targets.append(_tile_base(*_mirror(x, y)))

        drv = driver.Driver(self, 'driver', targets=targets, transfer_size=TRANSFER_SIZE, packet_size=PACKET_SIZE)

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
    gapy_description = "Array of hybrid compute+memory tiles connected through a FlooNoc mesh"
    model = Chip
    name = "my_system"
