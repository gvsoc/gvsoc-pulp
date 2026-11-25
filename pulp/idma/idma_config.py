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

from config_tree import Config, cfg_field


class IdmaConfig(Config):
    """Parameters of an iDMA top component, shared by its front-end, middle-end
    and back-ends in C++. Each top fills the ones its sub-blocks use."""

    transfer_queue_size: int = cfg_field(default=0, desc=(
        "Number of transfer requests which can be queued to the middle-end"
    ))
    burst_queue_size: int = cfg_field(default=0, desc=(
        "Maximum number of outstanding burst requests"
    ))
    burst_size: int = cfg_field(default=0, desc=(
        "Maximum burst size, in bytes, 0 for unconstrained"
    ))
    loc_base: int = cfg_field(default=0, fmt="hex", desc=(
        "Base address of the local area, used to tell the TCDM backend from the AXI one"
    ))
    loc_size: int = cfg_field(default=0, fmt="hex", desc=(
        "Size of the local area"
    ))
    tcdm_width: int = cfg_field(default=0, desc=(
        "Width of one TCDM access, in bytes"
    ))
    nb_cores: int = cfg_field(default=0, desc=(
        "Number of cores, each with its own register file port and completion event (reg32_3d front-end)"
    ))
    nb_ports: int = cfg_field(default=0, desc=(
        "Number of register file ports, the per-core ones first (reg32_3d front-end)"
    ))
    nb_streams: int = cfg_field(default=0, desc=(
        "Number of streams, each with its own transfer identifier counters (reg32_3d front-end)"
    ))
    multireg_count: int = cfg_field(default=0, desc=(
        "Number of entries of each of the status, next_id and done_id multiregs (reg32_3d front-end)"
    ))
    global_queue_depth: int = cfg_field(default=0, desc=(
        "Number of transfers of one stream accepted before the middle-end takes them over "
        "(reg32_3d front-end)"
    ))
    nb_groups: int = cfg_field(default=0, desc=(
        "Number of groups of back-ends (distributed iDMA)"
    ))
    nb_dmas_per_group: int = cfg_field(default=0, desc=(
        "Number of back-ends per group (distributed iDMA)"
    ))
    be_width: int = cfg_field(default=0, desc=(
        "Size in bytes of the local region handled by one back-end (distributed iDMA)"
    ))
