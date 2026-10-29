# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync


class EventUnit(Component):
    """PULP cluster hardware event unit (io_v2), with the timing of
    event_unit_flex: a wait read finding its software event already buffered
    is answered as a plain 1-cycle read, a sleeping core is answered one cycle
    after its event is buffered, a barrier event comes one cycle later than a
    software one, and mutex and dispatch reads always complete through the
    event of the core.

    Barriers, dispatch FIFO, hardware mutexes, per-core event/IRQ buffers and
    the SoC-event FIFO. Each core has a shared register view (i_INPUT) and a
    per-core demux fast path (i_DEMUX_IN) used for the blocking sleep/wait ops.
    """

    def __init__(self, parent: Component, name: str, nb_core: int = 9,
                 nb_mutexes: int = 1, dispatch_size: int = 8, nb_barriers: int = 9,
                 nb_fifo_events: int = 8, fifo_event: int = 27,
                 barrier_event: int = 16, mutex_event: int = 17,
                 dispatch_event: int = 18):
        super().__init__(parent, name)

        self.add_sources([
            'ips/pulp/event_unit/v3/event_unit.cpp',
            'ips/pulp/event_unit/v3/core_event_unit.cpp',
            'ips/pulp/event_unit/v3/mutex_unit.cpp',
            'ips/pulp/event_unit/v3/dispatch_unit.cpp',
            'ips/pulp/event_unit/v3/barrier_unit.cpp',
            'ips/pulp/event_unit/v3/soc_event_unit.cpp',
        ])

        # The C++ model reads these via recursive ("**/") js-config lookups.
        self.add_properties({
            'nb_core': nb_core,
            'properties': {
                'dispatch':   {'size': dispatch_size},
                'mutex':      {'nb_mutexes': nb_mutexes},
                'barriers':   {'nb_barriers': nb_barriers},
                'soc_event':  {'nb_fifo_events': nb_fifo_events, 'fifo_event': fifo_event},
                'events': {
                    'barrier':   barrier_event,
                    'mutex':     mutex_event,
                    'dispatch':  dispatch_event,
                },
            },
        })

        self.nb_core = nb_core

    # -------- ports --------

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2Sync())

    def i_DEMUX_IN(self, core: int) -> SlaveItf:
        return SlaveItf(self, f'demux_in_{core}', signature=IoV2Sync())

    def o_IRQ_REQ(self, core: int, itf: SlaveItf):
        self.itf_bind(f'irq_req_{core}', itf, signature='wire<int>')

    def i_IRQ_ACK(self, core: int) -> SlaveItf:
        return SlaveItf(self, f'irq_ack_{core}', signature='wire<int>')

    def o_CLOCK(self, core: int, itf: SlaveItf):
        self.itf_bind(f'clock_{core}', itf, signature='wire<bool>')

    def i_IN_EVENT(self, event: int, core: int) -> SlaveItf:
        return SlaveItf(self, f'in_event_{event}_pe_{core}', signature='wire<bool>')

    def i_SOC_EVENT(self) -> SlaveItf:
        return SlaveItf(self, 'soc_event', signature='wire<int>')
