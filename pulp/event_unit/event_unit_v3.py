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
from ips.pulp.event_unit.v3.event_unit import EventUnitConfig


def _get(config: dict, path: str):
    for name in path.split('/'):
        if not isinstance(config, dict) or name not in config:
            return 0
        config = config[name]
    return int(config, 0) if isinstance(config, str) else config


class Event_unit(st.Component):

    def __init__(self, parent, name, config):

        # config is the JSON-like description of the event unit, as found in the
        # chip property files; a missing entry is 0
        super(Event_unit, self).__init__(parent, name, config=EventUnitConfig(
            nb_core=_get(config, 'nb_core'),
            nb_mutexes=_get(config, 'properties/mutex/nb_mutexes'),
            dispatch_size=_get(config, 'properties/dispatch/size'),
            nb_barriers=_get(config, 'properties/barriers/nb_barriers'),
            nb_fifo_events=_get(config, 'properties/soc_event/nb_fifo_events'),
            fifo_event=_get(config, 'properties/soc_event/fifo_event'),
            barrier_event=_get(config, 'properties/events/barrier'),
            mutex_event=_get(config, 'properties/events/mutex'),
            dispatch_event=_get(config, 'properties/events/dispatch')))

        self.add_sources(['pulp/event_unit/eu_v3_impl.cpp'])
