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

"""Configuration dataclass for :class:`ips.pulp.idma_v3.cluster_dma.ClusterDmaV3`."""

from config_tree import Config, cfg_field
from ips.pulp.idma_v3.protocol_config import IdmaAxiConfig, IdmaObiConfig


class ClusterDmaV3Config(Config):
    """Configuration of the two-stream cluster iDMA (v3).

    The defaults describe the block as integrated in the PULP cluster: a
    64-bit AXI, 32-bit TCDM ports and the RTL parameters of its two back-ends.
    """

    data_width: int = cfg_field(default=8, desc=(
        "Width of the data path in bytes, a power of two (the RTL DataWidth "
        "/ 8): the AXI beat and the TCDM access."
    ))

    num_ax_in_flight: int = cfg_field(default=8, desc=(
        "Outstanding read and write bursts per stream (the RTL NumAxInFlight: "
        "depth of the r_dp_req / w_dp_req FIFOs)."
    ))

    buffer_depth: int = cfg_field(default=3, desc=(
        "Depth of every byte lane of the buffer between the read and the "
        "write manager (the RTL BufferDepth)."
    ))

    meta_fifo_depth: int = cfg_field(default=11, desc=(
        "Write bursts awaiting their response per stream (the RTL "
        "MetaFifoDepth); 0 derives it as buffer_depth + num_ax_in_flight."
    ))

    axi: IdmaAxiConfig = cfg_field(default_factory=lambda: IdmaAxiConfig(burst_len=5),
        desc="Parameters of the AXI managers (256 B bursts at 64 bits).")

    req_fifo_depth: int = cfg_field(default=8, desc=(
        "Transfers each stream's request FIFO can hold."
    ))

    nb_dims: int = cfg_field(default=3, desc=(
        "Dimensions the mid-end iterates (1 to 3)."
    ))

    obi: IdmaObiConfig = cfg_field(
        default_factory=lambda: IdmaObiConfig(port_width=4, addr_width=17), desc=(
        "Parameters of the TCDM (OBI) managers: 32-bit ports, the "
        "cluster-absolute address masked to the crossbar's local space."))

    nb_reg_ports: int = cfg_field(default=2, desc=(
        "Register access ports, each with its own copy of the transfer "
        "registers (cluster cores, FC)."
    ))

    nb_events: int = cfg_field(default=9, desc=(
        "Cores receiving the broadcast completion event."
    ))

    launch_bubble: int = cfg_field(default=1, desc=(
        "Cycles a launch from idle spends waking the datapath (the RTL "
        "datapath clock gate); 0 disables it."
    ))

    max_num_streams: int = cfg_field(default=16, desc=(
        "Length N of the STATUS / NEXT_ID / DONE_ID register arrays, which puts "
        "NEXT_ID_s at 0x004 + 4 (N + s) and DONE_ID_s at 0x004 + 4 (2N + s): "
        "16 in the upstream iDMA register file whatever the number of "
        "streams, at most 17. The entries of missing streams read as 0."
    ))
