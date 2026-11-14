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

import gvsoc.systree as st
from config_tree import Config, cfg_field


class GpioConfig(Config):
    """Configuration of the GPIO controller."""

    nb_gpio: int = cfg_field(default=32, desc="Number of GPIOs")
    soc_event: int = cfg_field(default=-1, desc="SoC event raised on GPIO interrupts, -1 for none")


class Gpio(st.Component):

    def __init__(self, parent, name, nb_gpio: int=32, soc_event: int=-1):

        super(Gpio, self).__init__(parent, name, config=GpioConfig(
            nb_gpio=nb_gpio, soc_event=soc_event))

        self.add_sources(['pulp/gpio/gpio_v3_impl.cpp'])
