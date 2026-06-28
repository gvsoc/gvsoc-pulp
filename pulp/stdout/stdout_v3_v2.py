# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0

import gvsoc.systree


class StdoutV2(gvsoc.systree.Component):
    """io_v2 sibling of :class:`pulp.stdout.stdout_v3.Stdout`. Same buffering
    semantics; only the input port speaks the v2 IO protocol."""

    def __init__(self,
                 parent,
                 name,
                 max_cluster=33,
                 max_core_per_cluster=16,
                 user_set_core_id=0xdeadbeef,
                 user_set_cluster_id=0xdeadbeef):

        super().__init__(parent, name)

        self.set_component('pulp.stdout.stdout_v3_impl_v2')
        self.add_properties({
            'max_cluster': max_cluster,
            'max_core_per_cluster': max_core_per_cluster,
            'user_set_core_id': user_set_core_id,
            'user_set_cluster_id': user_set_cluster_id,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io_v2')
