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

"""Configuration dataclass for :class:`ips.pulp.idma_v3.snitch_dma.SnitchDmaV3`."""

from config_tree import Config, cfg_field
from ips.pulp.idma_v3.protocol_config import IdmaAxiConfig


class SnitchDmaV3Config(Config):
    """Configuration of the Snitch xdma-driven iDMA (v3).

    The defaults are the Spatz cluster DMA: 512-bit data path, 3 bursts in
    flight, 4 KiB pages, RAW coupler, a 3-entry request FIFO and a 2D
    mid-end.
    """

    data_width: int = cfg_field(default=64, desc=(
        "Width of the data path and of the AXI masters in bytes, a power of "
        "two (the RTL DataWidth / 8)."
    ))

    num_ax_in_flight: int = cfg_field(default=3, desc=(
        "Outstanding read and write bursts (the RTL NumAxInFlight: depth of "
        "the r_dp_req / w_dp_req FIFOs)."
    ))

    buffer_depth: int = cfg_field(default=3, desc=(
        "Depth of every byte lane of the buffer between the read and the "
        "write manager (the RTL BufferDepth)."
    ))

    meta_fifo_depth: int = cfg_field(default=6, desc=(
        "Write bursts awaiting their response (the RTL MetaFifoDepth); 0 "
        "derives it as buffer_depth + num_ax_in_flight."
    ))

    axi: IdmaAxiConfig = cfg_field(default_factory=lambda: IdmaAxiConfig(burst_len=6),
        desc="Parameters of the AXI read and write managers.")

    raw_coupling: bool = cfg_field(default=True, desc=(
        "Read/write channel coupler (the RTL RAWCouplingAvail): the address "
        "phase of a write burst waits for the first response beat of a read "
        "burst unless the transfer decouples it."
    ))

    req_fifo_depth: int = cfg_field(default=3, desc=(
        "Transfers the request FIFO in front of the mid-end can hold."
    ))

    nb_dims: int = cfg_field(default=2, desc=(
        "Dimensions the mid-end iterates (1 to 3)."
    ))

    offload_req_latency: int = cfg_field(default=0, desc=(
        "Cycles between the offload of a dmcpy by the core and the entry of "
        "its transfer into the mid-end (1 for the Snitch RegisterOffloadReq "
        "register)."
    ))
