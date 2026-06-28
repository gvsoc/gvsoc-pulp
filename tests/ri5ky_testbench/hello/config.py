# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0

import pulpos


def declare(target):

    hello = pulpos.new_executable('test', target)

    hello.set_optimization_level('-Os -g')
    hello.add_sources('test.c')
