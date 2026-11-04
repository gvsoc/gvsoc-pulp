# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Siracusa PLL (pll_config registers).

Registers STATUS (0x0), CFG1 (0x4), CFG2 (0x8) and CFG3 (0xC), as the Siracusa
SDK programs them (hal/chips/siracusa/pll_regs.h). The output clock is
FBDIV * reference / (POSTDIV + 1) / (VCODIVSEL + 1) while the PLL is enabled
(CFG1.PLLEN), the reference being ref_frequency, and the PLL is always locked.
The reset value of CFG2 gives 50 MHz, as on the chip.
"""

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync


class SiracusaPllConfig(Config):
    ref_frequency: int = cfg_field(default=32768, desc="Frequency of the PLL reference clock")
    cfg1_reset:    int = cfg_field(default=0x00000001, fmt="hex", desc="Reset value of CFG1 (enabled)")
    cfg2_reset:    int = cfg_field(default=0x00040BEC, fmt="hex", desc=(
        "Reset value of CFG2 (FBDIV 3052, VCODIVSEL 1: 50 MHz)"))
    cfg3_reset:    int = cfg_field(default=0x00000000, fmt="hex", desc="Reset value of CFG3")


class SiracusaPll(Component):

    def __init__(self, parent: Component, name: str, config: SiracusaPllConfig = None):
        if config is None:
            config = SiracusaPllConfig()
        super().__init__(parent, name, config=config)

        self.add_sources(['ips/pulp/pll/siracusa_pll.cpp'])

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2Sync())

    def o_CLOCK(self, itf: SlaveItf):
        """Output clock, towards the control port of a clock domain."""
        self.itf_bind('clock_out', itf, signature='clock')
