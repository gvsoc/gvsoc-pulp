#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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
import gvsoc.systree


class SoftHierTargetBase(gvsoc.runner.Target):
    """
    Shared Target base class for every SoftHier topology.

    NOTE: Each topology's target module is a small subclass:

        from pulp.chips.softhier.softhier_target_base import SoftHierTargetBase
        from pulp.chips.softhier.softhier.softhier_system import SoftHierPlatform

        class Target(SoftHierTargetBase):
            model = SoftHierPlatform
            name = "softhier"

    NOTE: to port to gvrun with single target definition
    """
    gapy_description = "SoftHier Platform"
    model = None

    def __init__(self, parser, options=None, name=None):
        super(SoftHierTargetBase, self).__init__(parser, options,
            model=self.model, name=name)
