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

"""Configuration dataclass for :class:`ips.pulp.idma_v2.reg_dma.RegDmaV2`."""

from config_tree import Config, cfg_field


class RegDmaConfig(Config):
    """Configuration for the io_v2 register-front-end iDMA.

    Attributes
    ----------
    transfer_queue_size : int
        Maximum number of in-flight transfer descriptors queued in
        the register front-end. Default 8.
    burst_queue_size : int
        Maximum number of in-flight bursts owned by each back-end's
        slot pool. Default 8.
    burst_size : int
        Optional cap on a logical burst's size (bytes). ``0`` means
        no cap; the back-end uses the AXI page size (4 KiB) as the
        natural upper bound. Default 0.
    axi_width : int
        Width of the AXI bus in bytes. Used as the beat size on the
        AXI back-end: a logical read burst is announced as one
        io_v2 req of size = total burst bytes and the
        :class:`utils.io_v2_beat_adapter.BeatResponseAdapter`
        spreads the ``ceil(total / axi_width)`` response beats one
        per cycle. Writes stream beat-by-beat onto the AXI master.
        Default 8.
    """

    transfer_queue_size: int = cfg_field(default=8, desc=(
        "Number of transfer requests which can be queued to the DMA."
    ))

    burst_queue_size: int = cfg_field(default=8, desc=(
        "Maximum number of outstanding burst requests."
    ))

    burst_size: int = cfg_field(default=0, desc=(
        "Optional cap on a logical burst's size in bytes (0 = use the "
        "natural AXI-page upper bound of 4 KiB)."
    ))

    axi_width: int = cfg_field(default=8, desc=(
        "Width of the AXI interconnect, in bytes. Used as the beat size when "
        "the AXI backend streams a burst onto the io_v2 master."
    ))
