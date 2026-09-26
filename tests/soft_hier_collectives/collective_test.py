# SPDX-License-Identifier: Apache-2.0
import os
import gvsoc.systree as st
import gvsoc.runner
from vp.clock_domain import Clock_domain
from pulp.chips.soft_hier_old.flex_mesh_noc_v2 import FlexMeshNoCV2


class Chip(st.Component):
    def __init__(self, parent, name=None):
        super().__init__(parent, name)
        clock = Clock_domain(self, 'clock', frequency=1000000000)
        soc = st.Component(self, 'soc')
        clock.o_CLOCK(soc.i_CLOCK())
        args = dict(width=128, nb_x_clusters=4, nb_y_clusters=4,
                    ni_outstanding_reqs=int(os.environ.get('COLLECTIVE_NI_CAPACITY', '2')),
                    router_input_queue_size=int(os.environ.get('COLLECTIVE_ROUTER_CAPACITY', '1')))
        noc = FlexMeshNoCV2(soc, 'noc', **args)
        test = st.Component(soc, 'test')
        test.add_sources(['collective_probe.cpp'])
        for node in range(16):
            x, y = node % 4, node // 4
            test.itf_bind(f'out_{node}', noc.i_CLUSTER_INPUT(x, y), signature='io')
            noc.o_MAP(st.SlaveItf(test, f'mem_{node}', signature='io'),
                      base=0x30000000 + node * 0x10000, size=0x10000, x=x+1, y=y+1)


class Target(gvsoc.runner.Target):
    gapy_description = 'SoftHier collective multicast/reduction and contention regression'
    model = Chip
    name = 'collective_test'
