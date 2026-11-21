# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0

# spatz_v3 single-core with 8 FUs, FP32 only (spatz_cluster.single_core.hjson
# of the PULP / Snitch / Spatz comparison: no D, ELEN 32)

import importlib

from gvrun.target_qualifiers import set_default_attributes

_base = importlib.import_module('spatz_v3')


class Target(_base.Target):

    gapy_description = "Spatz virtual board (v3), single core, 8 FUs, FP32"

    def __init__(self, parser, options=None, name=None):
        set_default_attributes(['spatz/chip/soc/cluster/nb_core=1',
            'spatz/chip/soc/cluster/nb_lanes=8', 'spatz/chip/soc/cluster/fp32=1'])
        super().__init__(parser, options, name=name)
