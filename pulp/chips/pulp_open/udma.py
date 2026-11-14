#
# Copyright (C) 2020 GreenWaves Technologies
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
import os
from config_tree import Config, cfg_field


class UdmaSlotConfig(Config):
    """Peripheral area of a uDMA channel."""

    id: int = cfg_field(default=0, desc="Peripheral id")
    offset: int = cfg_field(default=0, fmt="hex", desc="Offset of the peripheral registers")


class UdmaEventConfig(Config):
    """Event of a uDMA channel."""

    event: int = cfg_field(default=-1, desc="Event number")


class UdmaInterfaceConfig(Config):
    """One kind of uDMA peripheral."""

    name: str = cfg_field(default='', desc="Kind of peripheral (spim, uart, hyper, ...)")
    version: int = cfg_field(default=0, desc="Version of the peripheral")
    nb_channels: int = cfg_field(default=0, desc="Number of instances of the peripheral")
    size: int = cfg_field(default=0, fmt="hex", desc="Size of the peripheral registers, 0 for one area")
    slots: list[UdmaSlotConfig] = cfg_field(default_factory=list, desc=(
        "Peripheral areas, the first nb_channels ones being the instances"))
    eot_events: list[UdmaEventConfig] = cfg_field(default_factory=list, desc=(
        "End-of-transfer event of each instance, for the SPI masters"))


class UdmaConfig(Config):
    """Configuration of the pulp-open uDMA."""

    nb_periphs: int = cfg_field(default=0, desc="Number of peripheral areas")
    l2_read_fifo_size: int = cfg_field(default=0, desc="Number of L2 reads in flight")
    interfaces: list[UdmaInterfaceConfig] = cfg_field(default_factory=list, desc=(
        "Peripherals of the uDMA"))


def _int(value):
    return int(value, 0) if isinstance(value, str) else value


class Udma(st.Component):
    def __init__(self, parent, name, config_file):

        config = UdmaConfig()
        super(Udma, self).__init__(parent, name, config=config)

        self.vcd_group(skip=True)

        self.add_sources([
            'pulp/udma/udma_v3_impl.cpp',
            'pulp/udma/uart/udma_uart_v1.cpp',
            'pulp/udma/hyper/udma_hyper_v3.cpp',
            'pulp/udma/spim/udma_spim_v3.cpp',
            'pulp/udma/i2s/udma_i2s_v2.cpp',
            'pulp/udma/cpi/udma_cpi_v1.cpp',
        ])

        self.add_c_flags([
            '-DUDMA_VERSION=3',
            '-DHYPER_VERSION=3',
            '-DI2S_VERSION=2',
            '-DHAS_UART',
            '-DHAS_HYPER',
            '-DHAS_SPIM',
            '-DHAS_I2S',
            '-DHAS_CPI',
        ])

        # The uDMA description is also read by the chip generators
        self.add_properties(self.load_property_file(config_file))

        config.nb_periphs = self.get_property('nb_periphs')
        config.l2_read_fifo_size = self.get_property('properties/l2_read_fifo_size')
        for itf_name in self.get_property('interfaces'):
            itf = self.get_property(itf_name)
            ids = itf['ids']
            offsets = itf['offsets']
            if len(ids) != len(offsets):
                raise RuntimeError(f'uDMA {itf_name}: ids and offsets have different sizes')
            config.interfaces.append(UdmaInterfaceConfig(name=itf_name,
                version=_int(itf['version']), nb_channels=_int(itf['nb_channels']),
                size=_int(itf.get('size', 0)),
                slots=[UdmaSlotConfig(id=_int(id), offset=_int(offset))
                    for id, offset in zip(ids, offsets)],
                eot_events=[UdmaEventConfig(event=_int(event))
                    for event in itf.get('eot_events', [])]))
