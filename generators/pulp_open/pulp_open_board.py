# Copyright (C) 2020  GreenWaves Technologies, SAS

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import gsystree as st
from pulp_open.pulp_open import Pulp_open
from devices.flash.hyperflash import Hyperflash
from devices.flash.spiflash import Spiflash
from devices.flash.atxp032 import Atxp032
from devices.ram.hyperram import Hyperram
from devices.testbench.testbench import Testbench
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
