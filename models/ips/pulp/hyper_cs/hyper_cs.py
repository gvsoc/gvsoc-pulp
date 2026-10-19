# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""HyperBus pads: routes a HyperBus master to the device of its active chip
select (the HyperBus part of the io (v1) padframe)."""

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf


class HyperCsConfig(Config):
    nb_cs: int = cfg_field(default=2, dump=True, desc="Number of chip selects")


class HyperCs(Component):

    def __init__(self, parent: Component, name: str, config: HyperCsConfig | None = None):
        if config is None:
            config = HyperCsConfig()
        super().__init__(parent, name, config=config)

        self.add_sources(['ips/pulp/hyper_cs/hyper_cs.cpp'])

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature='hyper')

    def o_CS(self, cs: int, itf: SlaveItf):
        self.itf_bind(f'cs{cs}', itf, signature='hyper')
