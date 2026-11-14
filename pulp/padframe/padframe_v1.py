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


class PadframeGroupConfig(Config):
    """One group of pads."""

    name: str = cfg_field(default='', desc="Name of the group, used for the port names")
    type: str = cfg_field(default='', desc="Interface of the group (qspim, uart, jtag, ...)")
    nb_cs: int = cfg_field(default=1, desc="Number of chip selects of qspim and hyper groups")
    is_master: bool = cfg_field(default=False, desc="True if the chip is master of the interface")
    is_slave: bool = cfg_field(default=False, desc="True if the chip is slave of the interface")


class PadframeConfig(Config):
    """Configuration of the padframe."""

    groups: list[PadframeGroupConfig] = cfg_field(default_factory=list, desc=(
        "Groups of pads, sorted by name"))


class Padframe(st.Component):

    def __init__(self, parent, name, config_file=None):

        config = PadframeConfig()
        super(Padframe, self).__init__(parent, name, config=config)

        # The padframe description is also read by the chip generators
        self.add_properties(self.load_property_file(config_file))

        self.add_sources(['pulp/padframe/padframe_v1_impl.cpp'])

        groups = self.get_property('groups')
        for group_name in sorted(groups.keys()):
            group = groups[group_name]
            if group.get('type') is None:
                continue
            config.groups.append(PadframeGroupConfig(name=group_name, type=group['type'],
                nb_cs=group.get('nb_cs', 1), is_master=group.get('is_master', False),
                is_slave=group.get('is_slave', False)))
