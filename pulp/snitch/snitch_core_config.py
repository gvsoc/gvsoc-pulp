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


from dataclasses import dataclass
from gvrun.config import Config, cfg_field


@dataclass(repr=False)
class SnitchCoreConfig(Config):
    nb_outstanding: int = cfg_field(default=1, dump=True, desc=(
        "Maximum number of oustanding requests"
    ))

    isa: str = cfg_field(default='rv32imafdc', dump=True, desc=(
        "ISA string of the core"
    ))
