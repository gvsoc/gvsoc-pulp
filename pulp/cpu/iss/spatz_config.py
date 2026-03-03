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

#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#


from dataclasses import dataclass
from gvrun.config import cfg_field
from cpu.iss_v2.riscv_config import RiscvConfig


@dataclass(repr=False)
class SpatzConfig(RiscvConfig):
    isa: str = cfg_field(default='rv32imafdcv', dump=True, desc=(
        "ISA string of the core"
    ))
    vlen: int = cfg_field(default=512, dump=True, desc=(
        "RISCV VLEN in bits."
    ))
    nb_lanes: int = cfg_field(default=4, dump=True, desc=(
        "Number of lanes."
    ))
    lane_width: int = cfg_field(default=8, dump=True, desc=(
        "Lane width in bytes. This sets the width of LSU and compute units."
    ))
