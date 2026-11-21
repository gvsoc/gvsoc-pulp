# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0

# pulp-open with a private FPU per core and an FP32 FMA unit with one pipeline
# register (the PULP cluster of the PULP / Snitch / Spatz comparison). With
# --platform rtl, the RTL compiled with them (tests/pulp-open/flist.py
# --nb-fpu 8 --fp32-latency 1, work/svsim_fpu8_lat1).

import importlib

from gvrun.target_qualifiers import set_default_attributes

_base = importlib.import_module('pulp-open')


class Target(_base.Target):

    gapy_description = "Pulp-open virtual board, private FPUs"

    def __init__(self, parser, options=None, name=None):
        set_default_attributes(['cluster/nb_fpu=8', 'cluster/fp32_latency=1'])
        super().__init__(parser, options, name=name)
