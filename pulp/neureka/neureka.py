#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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

"""Neureka neural engine of Siracusa on io_v2.

The streamer reaches the L1 through the HCI wide (HWPE) port (o_TCDM), with
one all-or-nothing request per access, and reads the weights from the weight
memory when the job asks for it (o_WMEM). The register file is i_INPUT and the
end of a job raises o_IRQ, bound to the event unit of every core.
"""

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync, IoV2SingleReq


class NeurekaConfig(Config):
    tp_in:           int = cfg_field(default=32, desc="Input channels processed in parallel")
    tp_in_s:         int = cfg_field(default=28, desc="Input channels processed in parallel in depthwise mode")
    tp_out:          int = cfg_field(default=32, desc="Output channels processed in parallel")
    qa_in:           int = cfg_field(default=8,  desc="Input activation bits")
    qa_out:          int = cfg_field(default=8,  desc="Output activation bits")
    h_size:          int = cfg_field(default=6,  desc="Height of the output patch")
    w_size:          int = cfg_field(default=6,  desc="Width of the output patch")
    column_size:     int = cfg_field(default=9,  desc="Size of a column of the compute array")
    block_size:      int = cfg_field(default=32, desc="Size of a block of the compute array")
    f_buffer_size:   int = cfg_field(default=8,  desc="Size of the feature buffer")
    filter_size:     int = cfg_field(default=3,  desc="Filter size")
    shift_cycles:    int = cfg_field(default=2,  desc="Cycles of a shift")
    overhead_ld_1x1: int = cfg_field(default=19, desc="Load overhead of 1x1 layers")
    overhead_ld_3x3: int = cfg_field(default=31, desc="Load overhead of 3x3 layers")
    overhead_mv:     int = cfg_field(default=17, desc="Matrix-vector overhead")
    quant_per_cycle: int = cfg_field(default=4,  desc="Quantizations per cycle")


class Neureka(Component):

    def __init__(self, parent: Component, name: str, config: NeurekaConfig | None = None):
        if config is None:
            config = NeurekaConfig()
        super().__init__(parent, name, config=config)

        self.set_component('pulp.neureka.neureka')

        self.add_properties({
            "tp_in"             :   config.tp_in,
            "tp_in_s"           :   config.tp_in_s,
            "tp_out"            :   config.tp_out,
            "qa_in"             :   config.qa_in,
            "qa_out"            :   config.qa_out,
            "h_size"            :   config.h_size,
            "w_size"            :   config.w_size,
            "column_size"       :   config.column_size,
            "block_size"        :   config.block_size,
            "f_buffer_size"     :   config.f_buffer_size,
            "filter_size"       :   config.filter_size,
            "shift_cycles"      :   config.shift_cycles,
            "overhead_ld_1x1"   :   config.overhead_ld_1x1,
            "overhead_ld_3x3"   :   config.overhead_ld_3x3,
            "overhead_mv"       :   config.overhead_mv,
            "quant_per_cycle"   :   config.quant_per_cycle,
        })

    def i_INPUT(self) -> SlaveItf:
        """Register file."""
        return SlaveItf(self, 'input', signature=IoV2Sync())

    def o_TCDM(self, itf: SlaveItf):
        """Streamer, towards the HCI wide input."""
        self.itf_bind('out', itf, signature=IoV2SingleReq())

    def o_WMEM(self, itf: SlaveItf):
        """Weight reads from the weight memory."""
        self.itf_bind('wmem_out', itf, signature=IoV2SingleReq())

    def o_IRQ(self, itf: SlaveItf):
        """End of job, to the event unit."""
        self.itf_bind('irq', itf, signature='wire<bool>')
