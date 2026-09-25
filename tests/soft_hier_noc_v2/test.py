# SPDX-License-Identifier: Apache-2.0
import gvsoc.systree as st
import gvsoc.runner
from gvsoc.signature import IoV2Beat
from vp.clock_domain import Clock_domain
from pulp.chips.soft_hier_old.flex_mesh_noc_v2 import FlexMeshNoCV2


class Chip(st.Component):
    def __init__(self, parent, name=None):
        super().__init__(parent, name)
        clock = Clock_domain(self, 'clock', frequency=1000000000)
        soc = st.Component(self, 'soc')
        clock.o_CLOCK(soc.i_CLOCK())
        noc = FlexMeshNoCV2(soc, 'noc', 128, 2, 2, ni_outstanding_reqs=2,
                           router_input_queue_size=1)
        test = st.Component(soc, 'test')
        test.add_sources(['bridge_test.cpp'])
        for port, x, y in [(0, 0, 0), (1, 0, 0), (2, 1, 1)]:
            test.itf_bind(f'out_{port}', noc.i_CLUSTER_INPUT(x, y), signature='io')
        noc.o_MAP(st.SlaveItf(test, 'memory', signature='io'),
                  base=0x180000000, size=0x20000, x=0, y=1)
        probe = st.Component(soc, 'probe')
        probe.add_sources(['response_probe.cpp'])
        # A border NI that has no legacy ingress adapter exercises native v2
        # response denial/retry alongside the three legacy initiators.
        probe.itf_bind('output', noc.i_WIDE_INPUT(3, 1), signature=IoV2Beat(128))
        probe.itf_bind('done', st.SlaveItf(test, 'probe_done', signature='wire<bool>'), signature='wire<bool>')


class Target(gvsoc.runner.Target):
    gapy_description = 'SoftHier v1/v2 NoC bridge and flow-control regression'
    model = Chip
    name = 'test'
