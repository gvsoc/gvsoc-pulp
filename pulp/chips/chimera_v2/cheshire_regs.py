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

"""Cheshire SoC registers of the Chimera host (io_v2).

Register map of the chimera-sdk ``cheshire_regs.h``, 32-bit accesses only:

  0x00-0x3C  SCRATCH_0..15  read/write. Writing SCRATCH_2 with bit 0 set ends
                            the simulation with bits 31:1 as status (the
                            end-of-computation convention of the Cheshire
                            runtime).
  0x40       BOOT_MODE      read-only
  0x44       RTC_FREQ       read-only, frequency of the RTC driving the CLINT
  0x48       PLATFORM_ROM   read-only, where the boot ROM jumps: the entry
                            point of the loaded binary
  0x4C       NUM_INT_HARTS  read-only
  0x50-0x58  HW_FEATURES, LLC_SIZE, VGA_PARAMS, read-only

Other offsets read as zero and ignore writes.
"""

from __future__ import annotations

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync


class CheshireRegsConfig(Config):
    rtc_freq:      int = cfg_field(default=32768, dump=True, desc="Value of RTC_FREQ, in Hz")
    boot_mode:     int = cfg_field(default=0, dump=True, desc="Value of BOOT_MODE")
    num_int_harts: int = cfg_field(default=1, dump=True, desc="Value of NUM_INT_HARTS")
    hw_features:   int = cfg_field(default=0, fmt="hex", dump=True, desc="Value of HW_FEATURES")
    llc_size:      int = cfg_field(default=0, fmt="hex", dump=True, desc="Value of LLC_SIZE")


class CheshireRegs(Component):

    def __init__(self, parent: Component, name: str, config: CheshireRegsConfig = None):
        if config is None:
            config = CheshireRegsConfig()
        super().__init__(parent, name, config=config)

        self.add_sources(['pulp/chips/chimera_v2/cheshire_regs.cpp'])

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2Sync())

    def i_ENTRY(self) -> SlaveItf:
        """Entry point of the loaded binary, read back through PLATFORM_ROM."""
        return SlaveItf(self, 'entry', signature='wire<uint64_t>')
