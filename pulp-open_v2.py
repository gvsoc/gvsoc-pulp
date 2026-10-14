# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

# pulp-open built from io_v2 components and iss_v2 cores (see
# pulp/chips/pulp_open_v2/pulp_open_v2.py).

import gvsoc.runner
from pulp.chips.pulp_open_v2.pulp_open_v2 import PulpOpenV2Board


class Target(gvsoc.runner.Target):

    gapy_description = "Pulp-open virtual board (io_v2 and iss_v2)"
    model = PulpOpenV2Board
    name = "pulp-open_v2"

    def __init__(self, parser, options=None, name=None):
        super(Target, self).__init__(parser, options,
            model=PulpOpenV2Board, name=name)
