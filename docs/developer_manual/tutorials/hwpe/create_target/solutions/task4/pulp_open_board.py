#
# Copyright (C) 2020-2026 ETH Zurich and University of Bologna
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

import os
import gvsoc.systree as st

#### SOLUTION - Change the path from pulp.chips.pulp_open.pulp_open to pulp.chips.pulp_open_hwpe.pulp_open

from pulp.chips.pulp_open_hwpe.pulp_open import Pulp_open
from devices.hyperbus.hyperflash import Hyperflash
from devices.spiflash.spiflash import Spiflash
from devices.spiflash.atxp032 import Atxp032
from devices.hyperbus.hyperram import Hyperram
from devices.testbench.testbench import Testbench
from devices.uart.uart_checker import Uart_checker
import gvsoc.runner
from pulp.chips.pulp_open.pulp_open import PulpOpenAttr
if os.environ.get('USE_GVRUN') is None:
    from gapylib.chips.pulp.flash import *
else:
    import gvrun.flash


class Pulp_open_board(st.Component):

    def __init__(self, parent, name, parser, options, use_ddr=False, pim_support=False, pulpnn=False):

        super(Pulp_open_board, self).__init__(parent, name, options=options)

        self.set_target_name('pulp-open')

        attr = PulpOpenAttr(self)
        self.set_attributes(attr)

        # Pulp
        pulp = Pulp_open(self, 'chip', attr, parser, use_ddr=use_ddr, pim_support=pim_support, pulpnn=pulpnn)

        # Flash
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
            default_content = os.path.join(
                os.path.dirname(__file__), 'default_flash_content.json')
            flash = gvrun.flash.Flash(
                name='hyperflash',
                size=8 * 1024 * 1024,
                attributes={'flash_type': 'hyper'},
                default_content_path=default_content,
            )
            self.register_flash(flash)
            # Point the Hyperflash C++ model at the image gvrun will write.
            # gvrun writes to {work_dir}/{image-basename}; GVSoC runs with
            # cwd == work_dir (see gvsoc/runner.py), so a relative path is
            # sufficient.
            hyperflash.add_property('content/image', flash.get_image_basename())

        self.bind(pulp, 'hyper0_cs1_data', hyperflash, 'input')

        self.bind(pulp, 'hyper0_cs0_data', hyperram, 'input')

        uart_checker = Uart_checker(self, 'uart_checker')
        self.bind(pulp, 'uart0', uart_checker, 'input')


class Pulp_open_nn_board(Pulp_open_board):
    def __init__(self, parent, name, parser, options, use_ddr=False):

        super().__init__(parent, name, parser, options, use_ddr, pulpnn=True)



class Pulp_open_board_ddr(Pulp_open_board):

    def __init__(self, parent, name, parser, options):
        super().__init__(parent, name, parser, options, use_ddr=True)

class Pulp_open_board_pim(Pulp_open_board):

    def __init__(self, parent, name, parser, options):
        super().__init__(parent, name, parser, options, use_ddr=True, pim_support=True)
