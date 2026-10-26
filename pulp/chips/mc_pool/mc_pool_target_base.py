#
# Copyright (C) 2026 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Author: Chi Zhang <chizhang@ethz.ch>

import gvsoc.runner


class McPoolTargetBase(gvsoc.runner.Target):
    """
    Shared Target base class for every multi-cluster MemPool configuration.

    NOTE: Each configuration's target module is a small subclass:

        from pulp.chips.mc_pool.mc_pool_target_base import McPoolTargetBase
        from pulp.chips.mc_pool.mc_pool_system_base import McPoolPlatform

        class Platform(McPoolPlatform):
            config = "tensorpool"

        class Target(McPoolTargetBase):
            model = Platform
            name = "mc_pool_tensorpool"
    """
    gapy_description = "Multi-cluster MemPool virtual board"
    model = None

    def __init__(self, parser, options=None, name=None):
        super(McPoolTargetBase, self).__init__(parser, options,
            model=self.model, name=name)
