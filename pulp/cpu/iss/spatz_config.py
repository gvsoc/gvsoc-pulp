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
from cpu.iss_v2.riscv_config import RiscvConfig

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
    nb_ipus: int = cfg_field(default=0, dump=True, desc=(
        "Number of integer units. Integer computational instructions are "
        "processed at this rate. Defaults to the number of lanes when 0."
    ))
    lane_width: int = cfg_field(default=8, dump=True, desc=(
        "Lane width in bytes. This sets the width of LSU and compute units."
    ))
    vlsu_v2: bool = cfg_field(default=False, dump=True, desc=(
        "If True, use the io_v2 variant of the spatz VLSU (vp/itf/io_v2.hpp). "
        "Selecting this also forces the scalar data LSU to its v2 variant "
        "since both share the same ISS translation unit."
    ))
    nb_outstanding_reqs: int = cfg_field(default=8, dump=True, desc=(
        "Depth of the per-port VLSU outstanding-request queue (reorder "
        "buffer). Matches num_spatz_outstanding_loads in the RTL cluster "
        "config (4 in the default spatz_cluster configuration)."
    ))
    lsu_nb_outstanding: int = cfg_field(default=1, dump=True, desc=(
        "Outstanding-access depth of the scalar data LSU (vlsu_v2 "
        "configurations only). The spatz_v3 cluster passes 5, calibrated "
        "against its RTL, whose FPU sequencer pipelines four scalar FP "
        "loads next to Snitch's single integer one. The default of 1 is "
        "the historical behaviour, kept for the other users of this core "
        "(the voscap CU controller and IMC cores), whose timing is locked "
        "by their own calibration tests."
    ))
    lsu_width: int = cfg_field(default=4, dump=True, desc=(
        "Width in bytes of the scalar data LSU port (vlsu_v2 "
        "configurations only): an access crossing a port-word boundary is "
        "split into two serialized beats. The spatz_v3 cluster passes 8, "
        "its scalar path being 64-bit end to end; the default of 4 is the "
        "historical behaviour, kept for the other users of this core."
    ))
