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


class RegDmaV3Config(Config):
    """Configuration of the single-stream register-driven iDMA (v3).

    The defaults describe an AXI-to-AXI instance with the RTL default
    back-end parameters; the Spatz cluster DMA is this with ``axi_width=64``,
    ``num_ax_in_flight=3``, ``meta_fifo_depth=6``, ``burst_len=6``,
    ``raw_coupling=True``, ``req_fifo_depth=3`` and ``nb_dims=2``.
    """

    axi_width: int = cfg_field(default=8, desc=(
        "Width of the data path and of the AXI masters in bytes (the RTL "
        "DataWidth / 8)."
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

    burst_len: int = cfg_field(default=8, desc=(
        "log2 of the AXI burst length in bus words: a burst never crosses a "
        "2^(log2(axi_width) + burst_len) byte boundary, capped at the 4 KiB "
        "AXI page (the RTL Burst_len, 8 upstream)."
    ))

    raw_coupling: bool = cfg_field(default=False, desc=(
        "Read/write channel coupler (the RTL RAWCouplingAvail): the address "
        "phase of a write burst waits for the first response beat of a read "
        "burst unless the transfer decouples it."
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
