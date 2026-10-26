# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""uDMA v3 of pulp-open on io_v2, with its UART and HyperBus peripherals.

The io (v1) model is pulp/udma/udma_v3_impl (configured by
pulp/chips/pulp_open/udma.json); this one has the same channels, registers
and timings, for the peripherals the pulp-open_v2 target has: UART 0 and the
HyperBus (its 8 transfer ids and its common registers).

Ports:

  * i_INPUT        — io_v2 sync slave, the APB registers
  * i_PERIPH_CLOCK — the peripheral clock, which paces the UART and HyperBus
                     bits (the uDMA itself is clocked by the SoC clock)
  * o_L2_RO        — io_v2 single-request master, the L2 reads of the TX channels
  * o_L2_WO        — io_v2 single-request master, the L2 writes of the RX channels
  * o_EVENT        — wire<int> master, the channel events to the SoC event unit
  * o_UART(i)      — uart master of UART i
  * o_HYPER(i)     — hyper master of HyperBus i
"""

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync, IoV2SingleReq


class UdmaV3Config(Config):
    nb_periphs: int = cfg_field(default=17, dump=True,
        desc="Number of peripheral areas (of 128 bytes, after the configuration one)")
    l2_read_fifo_size: int = cfg_field(default=8, dump=True,
        desc="Number of L2 reads of the TX channels in flight")
    nb_uart: int = cfg_field(default=1, dump=True, desc="Number of UARTs")
    uart_id: int = cfg_field(default=0, dump=True, desc="Peripheral id of UART 0")
    nb_hyper: int = cfg_field(default=1, dump=True, desc="Number of HyperBus interfaces")
    hyper_id: int = cfg_field(default=8, dump=True,
        desc="Peripheral id of HyperBus 0 (followed by its other transfer ids and its common registers)")
    hyper_nb_tran_ids: int = cfg_field(default=8, dump=True,
        desc="Number of transfer ids of each HyperBus, each one a peripheral area, the common registers being in the next one")


class UdmaV3(Component):

    def __init__(self, parent: Component, name: str, config: UdmaV3Config | None = None):
        if config is None:
            config = UdmaV3Config()
        super().__init__(parent, name, config=config)

        self.add_sources([
            'ips/pulp/udma/udma_v3.cpp',
            'ips/pulp/udma/uart_v1.cpp',
            'ips/pulp/udma/hyper_v3.cpp',
        ])

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2Sync())

    def i_PERIPH_CLOCK(self) -> SlaveItf:
        return SlaveItf(self, 'periph_clock', signature='clock')

    def o_L2_RO(self, itf: SlaveItf):
        self.itf_bind('l2_ro', itf, signature=IoV2SingleReq())

    def o_L2_WO(self, itf: SlaveItf):
        self.itf_bind('l2_wo', itf, signature=IoV2SingleReq())

    def o_EVENT(self, itf: SlaveItf):
        self.itf_bind('event_itf', itf, signature='wire<int>')

    def o_UART(self, uart: int, itf: SlaveItf):
        self.itf_bind(f'uart{uart}', itf, signature='uart')

    def o_HYPER(self, hyper: int, itf: SlaveItf):
        self.itf_bind(f'hyper{hyper}', itf, signature='hyper')
