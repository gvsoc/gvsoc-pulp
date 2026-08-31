#
# Copyright (C) 2026 Fondazione Chips-it
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

#
# Authors: Marco Paci, Fondazione Chips-it (marco.paci@chips.it)
#

# CV32E40P iss_v2 co-simulation target, no-PULP configuration (rv32imc, no CoreV extensions).

import gvsoc.runner
from pulp.cv32e40p_v2_standalone import Cv32e40pStandaloneTop


class Cv32e40p(Cv32e40pStandaloneTop):

    def __init__(self, parent, name=None):
        super().__init__(parent, name, pulp=0)


class Target(gvsoc.runner.Target):

    description = "CV32E40P iss_v2 standalone for UVM co-simulation (no PULP)"
    model = Cv32e40p
    name = "cv32e40p-v2-standalone-nopulp"
