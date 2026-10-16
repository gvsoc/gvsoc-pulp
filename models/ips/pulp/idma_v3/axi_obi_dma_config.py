#
# Copyright (C) 2026 Fondazione Chips-IT
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
# Authors: Lorenzo Zuolo, Fondazione Chips-IT (lorenzo.zuolo@chips.it)
#

"""Configuration dataclass for :class:`ips.pulp.idma_v3.axi_obi_dma.AxiObiDmaV3`."""

from config_tree import Config, cfg_field


class AxiObiDmaV3Config(Config):
    """Configuration of the single-direction AXI/OBI iDMA (v3).

    The defaults are the RTL defaults of idma_backend_r_axi_w_obi /
    idma_backend_r_obi_w_axi behind an idma_reg32_3d front-end with one
    register port and one stream.
    """

    axi_to_obi: bool = cfg_field(default=True, desc=(
        "Direction of the channel: True reads AXI and writes OBI "
        "(idma_backend_r_axi_w_obi), False reads OBI and writes AXI "
        "(idma_backend_r_obi_w_axi)."
    ))

    axi_width: int = cfg_field(default=8, desc=(
        "Width of the data path and of the AXI master in bytes (the RTL "
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

    req_fifo_depth: int = cfg_field(default=8, desc=(
        "Transfers the request FIFO in front of the mid-end can hold (the "
        "RTL JobFifoDepth)."
    ))

    nb_dims: int = cfg_field(default=3, desc=(
        "Dimensions the mid-end iterates (1 to 3)."
    ))

    multireg_count: int = cfg_field(default=16, desc=(
        "Entries of each of the STATUS, NEXT_ID and DONE_ID multiregs. The "
        "register file of iDMA v0.6.4 reserves 16 of them whatever the number "
        "of streams; 0 packs them to the single stream."
    ))

    obi_port_width: int = cfg_field(default=8, desc=(
        "Width of one OBI port in bytes."
    ))

    obi_ports_per_access: int = cfg_field(default=1, desc=(
        "OBI ports driven together for one access: access width = "
        "obi_port_width * obi_ports_per_access, which must equal axi_width."
    ))

    obi_addr_width: int = cfg_field(default=0, desc=(
        "Address bits kept on the OBI ports (0 keeps the address whole)."
    ))

    launch_bubble: int = cfg_field(default=0, desc=(
        "Cycles a launch from idle spends waking the datapath (the RTL "
        "datapath clock gate); 0 disables it."
    ))
