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

"""Configuration dataclass for :class:`ips.pulp.idma_v3.reg_dma.RegDmaV3`."""

from config_tree import Config, cfg_field
from ips.pulp.idma_v3.protocol_config import IdmaPortConfig


class RegDmaV3Config(Config):
    """Configuration of the single-stream register-driven iDMA (v3).

    ``read.protocol`` and ``write.protocol`` select the upstream back-end:
    rw_axi (AXI, AXI, the default), r_axi_w_obi (AXI, OBI), r_obi_w_axi (OBI,
    AXI) or rw_obi (OBI, OBI). The defaults describe an AXI-to-AXI instance
    with the RTL default back-end parameters.
    """

    data_width: int = cfg_field(default=8, desc=(
        "Width of the data path in bytes, a power of two (the RTL DataWidth "
        "/ 8): the beat of an AXI side, the access of an OBI side."
    ))

    num_ax_in_flight: int = cfg_field(default=8, desc=(
        "Outstanding read and write bursts (the RTL NumAxInFlight: depth of "
        "the r_dp_req / w_dp_req FIFOs)."
    ))

    buffer_depth: int = cfg_field(default=3, desc=(
        "Depth of every byte lane of the buffer between the read and the "
        "write manager (the RTL BufferDepth)."
    ))

    meta_fifo_depth: int = cfg_field(default=0, desc=(
        "Write bursts awaiting their response (the RTL MetaFifoDepth); 0 "
        "derives it as buffer_depth + num_ax_in_flight."
    ))

    raw_coupling: bool = cfg_field(default=False, desc=(
        "Read/write channel coupler (the RTL RAWCouplingAvail): the address "
        "phase of a write burst waits for the first response beat of a read "
        "burst unless the transfer decouples it. Only rw_axi has it."
    ))

    read: IdmaPortConfig = cfg_field(default_factory=IdmaPortConfig, desc=(
        "Read side: protocol and its parameters."
    ))

    write: IdmaPortConfig = cfg_field(default_factory=IdmaPortConfig, desc=(
        "Write side: protocol and its parameters."
    ))

    req_fifo_depth: int = cfg_field(default=8, desc=(
        "Transfers the request FIFO in front of the mid-end can hold."
    ))

    nb_dims: int = cfg_field(default=3, desc=(
        "Dimensions the mid-end iterates (1 to 3)."
    ))

    launch_bubble: int = cfg_field(default=0, desc=(
        "Cycles a launch from idle spends waking the datapath (the RTL "
        "datapath clock gate); 0 disables it."
    ))

    max_num_streams: int = cfg_field(default=16, desc=(
        "Length N of the STATUS / NEXT_ID / DONE_ID register arrays, which puts "
        "NEXT_ID_s at 0x004 + 4 (N + s) and DONE_ID_s at 0x004 + 4 (2N + s): "
        "16 in the upstream iDMA register file whatever the number of "
        "streams, at most 17. The entries of missing streams read as 0."
    ))
