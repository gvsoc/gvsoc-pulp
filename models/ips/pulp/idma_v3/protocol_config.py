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

"""Configuration of the iDMA v3 managers, one class per protocol.

The DMA configurations keep the back-end parameters (data width, FIFO
depths) and embed these for the protocol-specific ones, so that a new
protocol adds a class here and a field to :class:`IdmaPortConfig`, not
fields to every DMA configuration. They are plain data
(``_defer_parent_init``), lowered to nested structs of the DMA's C++
configuration.
"""

from typing import ClassVar

from config_tree import Config, cfg_field

# Protocol encodings (idma_pkg::protocol_e, as software writes them in CONF)
PROTOCOL_AXI = 0
PROTOCOL_OBI = 1


class IdmaAxiConfig(Config):
    """Parameters of an AXI manager."""

    _defer_parent_init: ClassVar[bool] = True

    burst_len: int = cfg_field(default=8, desc=(
        "log2 of the burst length in bus words: a burst never crosses a "
        "2^(log2(data_width) + burst_len) byte boundary, capped at the 4 KiB "
        "AXI page (the RTL Burst_len, 8 upstream)."
    ))


class IdmaObiConfig(Config):
    """Parameters of an OBI manager.

    One access of the data width is split over data_width / port_width
    narrow ports driven together (the RTL mem_to_banks).
    """

    _defer_parent_init: ClassVar[bool] = True

    port_width: int = cfg_field(default=4, desc=(
        "Width of one port in bytes, dividing the data width."
    ))

    addr_width: int = cfg_field(default=0, desc=(
        "Address bits kept on the ports, masking the address to the local "
        "space of the memory interconnect (0 keeps it whole)."
    ))

    def nb_ports(self, data_width: int) -> int:
        """Ports driven together for one access of data_width bytes."""
        return data_width // self.port_width


class IdmaPortConfig(Config):
    """One side (read or write) of a DMA whose protocol is configurable.

    Only the block of the selected protocol is used.
    """

    _defer_parent_init: ClassVar[bool] = True

    protocol: int = cfg_field(default=PROTOCOL_AXI, desc=(
        "Protocol of the manager: 0 AXI, 1 OBI."
    ))

    axi: IdmaAxiConfig = cfg_field(default_factory=IdmaAxiConfig, desc=(
        "AXI parameters, when protocol is AXI."
    ))

    obi: IdmaObiConfig = cfg_field(default_factory=IdmaObiConfig, desc=(
        "OBI parameters, when protocol is OBI."
    ))
