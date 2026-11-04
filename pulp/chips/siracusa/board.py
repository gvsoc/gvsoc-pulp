# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Siracusa board on io_v2 and iss_v2 (target siracusa).

Forked from the pulp-open_v2 board (pulp/chips/pulp_open_v2): the Siracusa
chip, with its 9-core cluster, Neureka and its weight memory, built from io_v2
components and iss_v2 cores. The software sees the memory map of the io v1
Siracusa model.

The uDMA has its UART and HyperBus, with the board HyperFlash, HyperRAM and
UART checker. Not modelled: the other uDMA peripherals, the GPIOs, the debug
TAPs, the padframe (but its HyperBus chip selects), the FC instruction cache,
the AXI proxy and the L1 test-and-set alias.
"""

from __future__ import annotations

import os

from config_tree import Config, cfg_field
from gvsoc.systree import Component
from vp.clock_domain import ClockDomain, ClockDomainConfig
from utils.clock_generator import ClockGenerator, ClockGeneratorConfig
from pulp.chips.siracusa.soc import SiracusaSoc, SiracusaSocConfig
from devices.hyperbus.hyperflash import Hyperflash
from devices.hyperbus.hyperram import Hyperram
from devices.uart.uart_checker import Uart_checker
if os.environ.get('USE_GVRUN') is None:
    from gapylib.chips.pulp.flash import DefaultFlashRomV2
else:
    import gvrun.flash


class SiracusaConfig(Config):
    ref_clock_frequency: int = cfg_field(default=32768, desc="Reference clock frequency")
    soc: SiracusaSocConfig = cfg_field(init=False, desc="SoC")

    def __post_init__(self):
        super().__post_init__()
        self.soc = SiracusaSocConfig()


class Siracusa(Component):

    def __init__(self, parent: Component, name: str, config: SiracusaConfig):
        super().__init__(parent, name, config=config)

        ref_clock = ClockDomain(self, 'ref_clock', config=ClockDomainConfig(
            frequency=config.ref_clock_frequency))
        ref_clock_gen = ClockGenerator(self, 'ref_clock_gen', config=ClockGeneratorConfig())
        soc = SiracusaSoc(self, 'soc', config=config.soc)

        ref_clock.o_CLOCK(ref_clock_gen.i_CLOCK())
        ref_clock_gen.o_CLOCK_SYNC(soc.i_REF_CLOCK())
        ref_clock_gen.o_CLOCK_CTRL(ref_clock.i_CTRL())

        # The SoC is clocked by its own clock domain, driven by the SoC FLL
        soc.o_CLOCK_SOC(soc.i_CLOCK())

        # Pins of the uDMA peripherals
        for name in ['hyper0_cs0', 'hyper0_cs1', 'uart0']:
            self.bind(soc, name, self, name)


class SiracusaBoard(Component):

    def __init__(self, parent: Component, name: str, parser=None, options=None):
        super().__init__(parent, name, options=options)

        config = SiracusaConfig()

        chip = Siracusa(self, 'chip', config=config)

        # The HyperRAM and the HyperFlash, with its image, on the HyperBus of
        # the uDMA, and the UART checker on its UART, as the io v1 board
        hyperflash = Hyperflash(self, 'hyperflash')
        hyperram = Hyperram(self, 'ram')

        if os.environ.get('USE_GVRUN') is None:
            self.register_flash(
                DefaultFlashRomV2(self, 'hyperflash', image_name=hyperflash.get_image_path(),
                    flash_attributes={
                        "flash_type": 'hyper'
                    },
                    size=8*1024*1024
                ))
        else:
            default_content = os.path.join(os.path.dirname(__file__), 'default_flash_content.json')
            flash = gvrun.flash.Flash(
                name='hyperflash',
                size=8 * 1024 * 1024,
                attributes={'flash_type': 'hyper'},
                default_content_path=default_content,
            )
            self.register_flash(flash)
            # gvrun writes the image to {work_dir}/{image-basename}, where
            # GVSoC runs
            hyperflash.add_property('content/image', flash.get_image_basename())

        self.bind(chip, 'hyper0_cs1', hyperflash, 'input')
        self.bind(chip, 'hyper0_cs0', hyperram, 'input')

        uart_checker = Uart_checker(self, 'uart_checker')
        self.bind(chip, 'uart0', uart_checker, 'input')

