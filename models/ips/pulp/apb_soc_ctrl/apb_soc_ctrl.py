# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""PULP APB SoC control (io_v2).

io_v2 port of :class:`pulp.chips.pulp_open.apb_soc_ctrl.Apb_soc_ctrl`:

  0x04  BOOTADDR    FC boot address, forwarded on the bootaddr wire
  0xA0  CORESTATUS  writing bit 31 ends the simulation with bits 30:0 as status
  0xC4  PADS_CONFIG boot select pads (bootsel wire)

Other offsets are accepted and read as zero.
"""

from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync


class ApbSocCtrl(Component):

    def __init__(self, parent: Component, name: str):
        super().__init__(parent, name)

        self.add_sources(['ips/pulp/apb_soc_ctrl/apb_soc_ctrl.cpp'])

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2Sync())

    def i_BOOTSEL(self) -> SlaveItf:
        return SlaveItf(self, 'bootsel', signature='wire<int>')

    def o_BOOTADDR(self, itf: SlaveItf):
        self.itf_bind('bootaddr', itf, signature='wire<uint64_t>')
