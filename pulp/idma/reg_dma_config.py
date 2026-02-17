#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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
class RegDmaConfig(Config):

    transfer_queue_size: int = cfg_field(default=8, desc=(
        "Number of transfer requests which can be queued to the DMA."
    ))

    burst_queue_size: int = cfg_field(default=8, desc=(
        "Maximum number of outstanding burst requests."
    ))

    burst_size: int = cfg_field(default=0, desc=(
        "Burst size."
    ))

    loc_base: int = cfg_field(default=0, desc=(
        "Base address of the local area."
    ))

    loc_size: int = cfg_field(default=0, desc=(
        "Size of the local area."
    ))

    tcdm_width: int = cfg_field(default=0, desc=(
        "Width of the local interconnect, in bytes."
    ))
