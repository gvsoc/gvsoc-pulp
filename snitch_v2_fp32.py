# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0

# snitch_v2 FP32 (FLEN 32, 32-bit data width) with the timing of the PULP /
# Snitch / Spatz comparison (cfg/FP32_xdiv.json: 16 TCDM banks, one FMA
# pipeline register, 8 KiB icache). With --platform rtl, the RTL of
# tests/calibration/targets/snitch/rtl FP32=1 PAPER=1.

import importlib

from gvrun.target_qualifiers import set_default_attributes

_base = importlib.import_module('snitch_v2')


class Target(_base.Target):

    gapy_description = "Snitch virtual board (v2), FP32, comparison timing"

    def __init__(self, parser, options=None, name=None):
        set_default_attributes(['snitch/chip/soc/cluster/fp32=1',
            'snitch/chip/soc/cluster/paper=1'])
        super().__init__(parser, options, name=name)
