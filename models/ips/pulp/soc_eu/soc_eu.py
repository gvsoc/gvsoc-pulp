# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""PULP SoC Event Unit (SoC_EU).

APB-accessible event generator: masks/dispatches up to ~256 event IDs
to three targets (FC, CL, PR). Each target has an event queue
delivered via a `wire<int>` master port carrying the event ID. FC-side
queue backs the FC ITC's soc-event FIFO (IRQ 26).

Mirrors the RTL `soc_event_generator`: the unit owns one per-event-ID
rising-edge detector, hard-wired so bit position `j` of `per_events_i`
emits event `j`. We expose this as a flat array of `wire<bool>` slave
ports named `event_<id>`. Callers bind directly by event ID — the
binding alone determines which signal raises which event.

Ports:
  * i_INPUT          — APB slave (config registers)
  * i_REF_CLOCK      — slow reference clock (for the 21kHz event tick)
  * i_EVENT_IN       — wire<int> slave; sources that already emit an
                       event ID can push directly here (used by
                       addrgens, etc.)
  * i_EVENT(id)      — wire<bool> slave for the event-ID slot
  * o_FC_EVENT       — wire<int> master to FC ITC soc-event FIFO
  * o_CL_EVENT       — wire<int> master to cluster (not bound yet)
  * o_PR_EVENT       — wire<int> master to periph group (not bound yet)
  * o_REF_CLOCK_EVT  — wire<bool> master (ref-clock rising-edge pulse)
"""

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync


class SocEuConfig(Config):
    # Event id that the model raises on every ref-clock edge (used by the 21k
    # reference-timer event). -1 disables the behaviour.
    ref_clock_event: int = cfg_field(default=-1, dump=True,
        desc="Event ID raised on each ref-clock edge (-1 = disabled)")
    nb_fc_events:    int = cfg_field(default=8, dump=True,
        desc="Number of FC software-event bits in the SW_EVENT register")
    first_fc_event:  int = cfg_field(default=0, dump=True,
        desc="Event ID corresponding to SW_EVENT bit 0")
    nb_events:       int = cfg_field(default=256, dump=True,
        desc="Number of per-source event slots (per_events_i width)")


class SocEu(Component):

    def __init__(self, parent: Component, name: str,
                 config: SocEuConfig = None):
        if config is None:
            config = SocEuConfig()
        super().__init__(parent, name, config=config)

        self.add_sources(['ips/pulp/soc_eu/soc_eu.cpp'])

        self._nb_events = config.nb_events

    # -------- inputs --------

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, itf_name='input', signature=IoV2Sync())

    def i_REF_CLOCK(self) -> SlaveItf:
        return SlaveItf(self, itf_name='ref_clock', signature='clock')

    def i_EVENT_IN(self) -> SlaveItf:
        return SlaveItf(self, itf_name='event_in', signature='wire<int>')

    def i_EVENT(self, event_id: int) -> SlaveItf:
        """wire<bool> slave for the slot that raises `event_id` on
        rising edge. Mirrors `per_events_i[event_id]` in the RTL."""
        assert 0 <= event_id < self._nb_events, (
            f'event_id {event_id} out of range [0, {self._nb_events})')
        return SlaveItf(self, itf_name=f'event_{event_id}',
                        signature='wire<bool>')

    # -------- outputs --------

    def o_FC_EVENT(self, itf: SlaveItf):
        self.itf_bind('fc_event_itf', itf, signature='wire<int>')

    def o_CL_EVENT(self, itf: SlaveItf):
        self.itf_bind('cl_event_itf', itf, signature='wire<int>')

    def o_PR_EVENT(self, itf: SlaveItf):
        self.itf_bind('pr_event_itf', itf, signature='wire<int>')

    def o_REF_CLOCK_EVT(self, itf: SlaveItf):
        self.itf_bind('ref_clock_event', itf, signature='wire<bool>')
