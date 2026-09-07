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


class ClusterDmaV3Config(Config):
    """Configuration of the two-stream cluster iDMA (v3).

    The defaults describe the block as integrated in the PULP cluster: a
    64-bit AXI, 32-bit TCDM ports and the RTL parameters of its two back-ends.
    """

    axi_width: int = cfg_field(default=8, desc=(
        "Width of the data path and of the AXI masters in bytes (the RTL "
        "DataWidth / 8)."
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

    burst_len: int = cfg_field(default=5, desc=(
        "log2 of the AXI burst length in bus words: a burst never crosses a "
        "2^(log2(axi_width) + burst_len) byte boundary (the RTL Burst_len, "
        "256 B with the defaults)."
    ))

    req_fifo_depth: int = cfg_field(default=8, desc=(
        "Transfers each stream's request FIFO can hold."
    ))

    nb_dims: int = cfg_field(default=3, desc=(
        "Dimensions the mid-end iterates (1 to 3)."
    ))

    obi_port_width: int = cfg_field(default=4, desc=(
        "Width of one TCDM port in bytes."
    ))

    obi_ports_per_access: int = cfg_field(default=2, desc=(
        "TCDM ports driven together for one OBI access (the RTL "
        "mem_to_banks): access width = obi_port_width * obi_ports_per_access, "
        "which must equal axi_width."
    ))

    obi_addr_width: int = cfg_field(default=17, desc=(
        "Address bits kept on the TCDM ports: the cluster-absolute address "
        "is masked to the crossbar's local space (0 keeps it whole)."
    ))

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
