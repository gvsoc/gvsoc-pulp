#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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

import gsystree as st
from pulp.chips.pulp_open.pulp_open import Pulp_open
from devices.flash.hyperflash import Hyperflash
from devices.flash.spiflash import Spiflash
from devices.flash.atxp032 import Atxp032
from devices.ram.hyperram import Hyperram
from devices.testbench.testbench import Testbench
from devices.uart.uart_checker import Uart_checker
import gv.gvsoc_runner
from gapylib.chips.pulp.flash import *


class Pulp_open_board(gv.gvsoc_runner.Runner):

    def __init__(self, options, parent=None, name='top'):

        super(Pulp_open_board, self).__init__(parent, name, options=options)

        # Pulp
        pulp = Pulp_open(self, 'chip')

        # Flash
        hyperflash = Hyperflash(self, 'hyperflash')
        spiflash = Spiflash(self, 'spiflash')
        hyperram = Hyperram(self, 'ram')

        self.register_flash(
            DefaultFlashRomV2(self, 'flash', image_name=spiflash.get_image_path(),
                flash_attributes={
                    "flash_type": 'spi'
                },
                size=8*1024*1024
            ))

        self.bind(pulp, 'spim0_cs0', spiflash, 'cs')
        self.bind(pulp, 'spim0_cs0_data', spiflash, 'input')

        self.bind(pulp, 'hyper0_cs1', hyperflash, 'cs')
        self.bind(pulp, 'hyper0_cs1_data', hyperflash, 'input')

        self.bind(pulp, 'hyper0_cs0', hyperram, 'cs')
        self.bind(pulp, 'hyper0_cs0_data', hyperram, 'input')

        uart_checker = Uart_checker(self, 'uart_checker')
        self.bind(pulp, 'uart0', uart_checker, 'input')