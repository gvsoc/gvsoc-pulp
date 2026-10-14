# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""pulp-open on io_v2 and iss_v2 (target pulp-open_v2).

The same chip as the pulp-open target (pulp/chips/pulp_open), built from io_v2
components and iss_v2 cores: the FC and the 8 cluster PEs are iss_v2 Ri5ky
cores, the cluster IPs are the io_v2 ones of pulp/*/io_v2, and the
interconnects are router_v2 / log_ico_v2 / HCI. The software sees the same
memory map, so the PulpOS pulp-open port runs unchanged (the board has the
pulp-open target name and attributes).

Not modelled yet: the uDMA and its peripherals with the board flash and RAM,
the GPIOs, the debug TAPs, the padframe, the L1 test-and-set alias, NE16 and
RedMulE, and the PULP-NN / Xf8 / Xfaux ISA extensions.
"""

from __future__ import annotations

from config_tree import Config, cfg_field
from gvrun.attribute import Attr, get_attribute_arg_value
from gvsoc.systree import Component
from vp.clock_domain import ClockDomain, ClockDomainConfig
from utils.clock_generator import ClockGenerator, ClockGeneratorConfig
from pulp.chips.pulp_open.pulp_open import PulpOpenAttr
from pulp.chips.pulp_open_v2.soc import PulpOpenV2Soc, PulpOpenV2SocConfig


def _set_default(parent: Attr, name: str, value):
    # Changes the default value of an attribute, unless it is set from the
    # command line (reading an attribute returns its value, not the Value)
    attribute = object.__getattribute__(parent, name)
    if get_attribute_arg_value(attribute.get_path()) is None:
        attribute.value = value


class PulpOpenV2Config(Config):
    ref_clock_frequency: int = cfg_field(default=32768, desc="Reference clock frequency (the RTL testbench one)")
    soc: PulpOpenV2SocConfig = cfg_field(init=False, desc="SoC")

    def __post_init__(self):
        super().__post_init__()
        self.soc = PulpOpenV2SocConfig()


class PulpOpenV2(Component):

    def __init__(self, parent: Component, name: str, config: PulpOpenV2Config):
        super().__init__(parent, name, config=config)

        ref_clock = ClockDomain(self, 'ref_clock', config=ClockDomainConfig(
            frequency=config.ref_clock_frequency))
        ref_clock_gen = ClockGenerator(self, 'ref_clock_gen', config=ClockGeneratorConfig())
        soc = PulpOpenV2Soc(self, 'soc', config=config.soc)

        ref_clock.o_CLOCK(ref_clock_gen.i_CLOCK())
        ref_clock_gen.o_CLOCK_SYNC(soc.i_REF_CLOCK())
        ref_clock_gen.o_CLOCK_CTRL(ref_clock.i_CTRL())

        # The SoC is clocked by its own clock domain, driven by the SoC FLL
        soc.o_CLOCK_SOC(soc.i_CLOCK())


class PulpOpenV2Board(Component):

    def __init__(self, parent: Component, name: str, parser=None, options=None):
        super().__init__(parent, name, options=options)

        # Same target name and attributes as pulp-open, so that the software
        # is built by the same PulpOS port (pulpos/pulp-open.py)
        self.set_target_name('pulp-open')
        attr = PulpOpenAttr(self)
        self.set_attributes(attr)

        config = PulpOpenV2Config()

        # The software is linked for the L2 of this chip (the attributes have
        # the bigger one of the v1 pulp-open)
        soc = config.soc
        _set_default(attr.soc.l2.range, 'size', soc.l2_shared_base + soc.l2_shared_size - soc.l2_base)
        _set_default(attr.soc.l2.shared.range, 'size', soc.l2_shared_size)

        PulpOpenV2(self, 'chip', config=config)
