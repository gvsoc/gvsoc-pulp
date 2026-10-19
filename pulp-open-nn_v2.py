# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

# pulp-open-nn (pulp-open with the PULP-NN ISA extension) built from io_v2
# components and iss_v2 cores (see
# pulp/chips/pulp_open_v2/pulp_open_v2.py).

import gvsoc.runner
from pulp.chips.pulp_open_v2.pulp_open_v2 import PulpOpenNnV2Board


class Target(gvsoc.runner.Target):

    gapy_description = "Pulp-open virtual board with pulpNN (io_v2 and iss_v2)"
    model = PulpOpenNnV2Board
    name = "pulp-open-nn_v2"

    def __init__(self, parser, options=None, name=None):
        super(Target, self).__init__(parser, options,
            model=PulpOpenNnV2Board, name=name)
