# SPDX-License-Identifier: Apache-2.0
"""SoftHier-local FlooNoC v2 with legacy IO and collective DMA support."""
import gvsoc.systree as st
from gvsoc.signature import IoV2Beat
from pulp.chips.soft_hier_old.floonoc_v2.floonoc_v2 import FlooNocV2ClusterGridNarrowWide


class NocBridge(st.Component):
    def __init__(self, parent, name, width, capacity, legacy_input):
        super().__init__(parent, name)
        self.width = width
        self.legacy_input = legacy_input
        faces = {}
        for face in ('legacy', 'v2'):
            comp = st.Component(self, face)
            comp.add_sources([f'pulp/chips/soft_hier_old/noc_bridge_{face}.cpp'])
            comp.add_properties(dict(width=width, capacity=capacity,
                                     legacy_input=legacy_input, max_burst_size=4096))
            faces[face] = comp
        source, target = (faces['legacy'], faces['v2']) if legacy_input else (faces['v2'], faces['legacy'])
        self.bind(source, 'request', target, 'request')
        self.bind(target, 'done', source, 'done')
        self.itf_bind('input', st.SlaveItf(source, 'input', signature=self.input_signature),
                      signature=self.input_signature, composite_bind=True)
        target.itf_bind('output', st.SlaveItf(self, 'output', signature=self.output_signature),
                        signature=self.output_signature)
        if legacy_input:
            faces['v2'].itf_bind('collective', st.SlaveItf(self, 'collective',
                signature='wire<SoftHierCollective>'), signature='wire<SoftHierCollective>')

    @property
    def input_signature(self):
        return 'io' if self.legacy_input else IoV2Beat(self.width)

    @property
    def output_signature(self):
        return IoV2Beat(self.width) if self.legacy_input else 'io'

    def i_INPUT(self):
        return st.SlaveItf(self, 'input', signature=self.input_signature)

    def o_OUTPUT(self, itf):
        self.itf_bind('output', itf, signature=self.output_signature)


class FlexMeshNoCV2(FlooNocV2ClusterGridNarrowWide):
    def __init__(self, parent, name, width, nb_x_clusters, nb_y_clusters,
                 ni_outstanding_reqs=64, router_input_queue_size=2, narrow_width=8):
        for label, value in (('wide width', width), ('narrow width', narrow_width)):
            if not isinstance(value, int) or value <= 0 or value & (value-1):
                raise ValueError(f'{label} must be a positive power of two in bytes')
        if width > 4096 or ni_outstanding_reqs <= 0 or router_input_queue_size < 1:
            raise ValueError('Invalid v2 link width or queue capacity')
        super().__init__(parent, name, wide_width=width, narrow_width=narrow_width,
                         nb_x_clusters=nb_x_clusters, nb_y_clusters=nb_y_clusters,
                         ni_outstanding_reqs=ni_outstanding_reqs,
                         router_input_queue_size=router_input_queue_size)
        self.add_property('backend', 'floonoc_v2')
        self.add_property('width', width)  # Common benchmark configuration metadata.
        self.width, self.capacity = width, ni_outstanding_reqs
        self.ingress = {}
        for y in range(nb_y_clusters):
            for x in range(nb_x_clusters):
                bridge = NocBridge(self, f'ingress_{x+1}_{y+1}', width, self.capacity, True)
                bridge.o_OUTPUT(super().i_CLUSTER_WIDE_INPUT(x, y))
                bridge.itf_bind('collective', st.SlaveItf(self._fabric._nis[x+1, y+1][0],
                    'collective', signature='wire<SoftHierCollective>'),
                    signature='wire<SoftHierCollective>')
                self.ingress[x, y] = bridge
                self.itf_bind(f'legacy_input_{x+1}_{y+1}', bridge.i_INPUT(),
                              signature='io', composite_bind=True)

    def i_CLUSTER_INPUT(self, x, y):
        # Legacy IO supports fan-in and retains each request's response port.
        # Exactly one v2 master (the bridge) is bound to each NI input.
        return st.SlaveItf(self, f'legacy_input_{x+1}_{y+1}', signature='io')

    def o_MAP(self, itf, base, size, x, y):
        bridge = NocBridge(self, f'egress_{x}_{y}', self.width, self.capacity, False)
        port = f'legacy_target_{x}_{y}'
        bridge.o_OUTPUT(st.SlaveItf(self, port, signature='io'))
        self.itf_bind(port, itf, signature='io')
        super().o_MAP(base=base, size=size, x=x, y=y, rm_base=True)
        self.itf_bind(f'ni_wide_{x}_{y}', bridge.i_INPUT(),
                      signature=IoV2Beat(self.width), composite_bind=True)
