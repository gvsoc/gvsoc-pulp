
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

from config_tree import cfg_field
from pulp.cpu.iss.cva6_config import Cva6Config

class Cva6V2Config(Cva6Config):
    """Configuration of Cva6V2: the Cva6Config fields plus the frontend model ones."""
    branch_prediction: bool = cfg_field(default=True, dump=True, desc=(
        "True to model the CVA6 frontend: fetch of one 32-bit word per cycle, BHT/BTB/RAS "
        "branch prediction and the instruction queue. False keeps the plain single-line "
        "prefetcher, with no fetch or branch cost."
    ))
    bht_entries: int = cfg_field(default=128, dump=True, desc=(
        "Number of BHT 2-bit counters (BHTEntries), indexed by pc[log2(entries):1]."
    ))
    btb_entries: int = cfg_field(default=32, dump=True, desc=(
        "Number of BTB entries (BTBEntries), indexed by pc[log2(entries):1]."
    ))
    ras_depth: int = cfg_field(default=2, dump=True, desc=(
        "Depth of the return address stack (RASDepth)."
    ))
    fetch_queue_depth: int = cfg_field(default=8, dump=True, desc=(
        "Number of instructions the instruction queue holds between fetch and decode."
    ))
    fetch_queue_cf_depth: int = cfg_field(default=2, dump=True, desc=(
        "Number of predicted control flows the instruction queue holds (address FIFO depth)."
    ))
    fetch_queue_restart: int = cfg_field(default=1, dump=True, desc=(
        "Cycles between the instruction queue getting room and the next word being fetched."
    ))
    mispredict_penalty: int = cfg_field(default=6, dump=True, desc=(
        "Cycles between the dispatch of a mispredicted control flow and the dispatch of the "
        "first instruction on the right path."
    ))
