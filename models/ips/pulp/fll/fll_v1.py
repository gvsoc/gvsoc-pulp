# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""PULP FLL (fll_v1), io_v2 port of :class:`pulp.fll.fll_v1.Fll`.

Registers STATUS (0x0), CONF1 (0x4), CONF2 (0x8) and INTEGRATOR (0xC). The
output clock frequency follows the DCO model of the v1 FLL, from reset on. The
control interface is synchronous to the reference clock: an access is answered
on the next reference clock edge, one access per edge.
"""

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2SingleReq


class FllConfig(Config):
    status_reset:     int = cfg_field(default=0x00000000, fmt="hex", desc="Reset value of STATUS")
    conf1_reset:      int = cfg_field(default=0x454C05F5, fmt="hex", desc="Reset value of CONF1")
    conf2_reset:      int = cfg_field(default=0x02004109, fmt="hex", desc="Reset value of CONF2")
    integrator_reset: int = cfg_field(default=0x00000000, fmt="hex", desc="Reset value of INTEGRATOR")


class Fll(Component):

    def __init__(self, parent: Component, name: str, config: FllConfig = None):
        if config is None:
            config = FllConfig()
        super().__init__(parent, name, config=config)

        self.add_sources(['ips/pulp/fll/fll_v1.cpp'])

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2SingleReq())

    def i_REF_CLOCK(self) -> SlaveItf:
        return SlaveItf(self, 'ref_clock', signature='clock_gen')

    def o_CLOCK(self, itf: SlaveItf):
        """Output clock, towards the control port of a clock domain."""
        self.itf_bind('clock_out', itf, signature='clock')
