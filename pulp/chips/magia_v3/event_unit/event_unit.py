#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
#                    University of Bologna
# Copyright (C) 2026 Fondazione Chips-IT
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
from gvsoc.signature import IoV2SingleReq


class MagiaEventUnit(st.Component):
    """Event unit of the MAGIA tile, the io_v2 port of
    :class:`pulp.event_unit.event_unit_v3.Event_unit` (``event_unit.cpp``).

    Same event / mutex / dispatch / barrier / soc-event behaviour; only the
    memory-mapped ports move to the v2 IO protocol.

    The ports are IoV2SingleReq, not IoV2Sync: an event wait (or a mutex /
    dispatch sleep) parks the request and replies later from the wake-up event,
    i.e. the unit answers IO_REQ_GRANTED + a deferred ``resp()``. Every access
    is a single 32-bit word, so a single-beat response always covers it.

    No ``width`` is declared on the ports: the model rejects anything but a
    32-bit access with a warning, exactly like the v1 model, instead of having
    the framework insert a width adapter that would silently split wider
    accesses.

    Each core gets ``irq_req_<n>`` (identifier of the requested event, -1 for
    none) and ``irq_ack_<n>``, as for a core with a PULP interrupt interface,
    or ``irq_line_<n>``, the level of the request, for a core with RISC-V
    interrupt lines such as the CV32E40P, whose acknowledge then clears the
    requested event whatever identifier it carries. ``clock_<n>`` gates the
    core while it waits.

    Parameters
    ----------
    parent : st.Component
        Parent component.
    name : str
        Local name of the event unit within ``parent``.
    nb_core : int
        Number of cores.
    nb_mutexes, nb_barriers : int
        Number of mutexes and of hardware barriers.
    dispatch_size : int
        Depth of the dispatch FIFO.
    nb_fifo_events, fifo_event : int
        Depth of the soc event FIFO and the event it raises.
    dispatch_event, mutex_event, barrier_event : int
        Events raised by the dispatch, mutex and barrier units.
    wakeup_req_latency : int
        Cycles added to the reply of an event wait on top of the wake-up
        itself (6, the value of the v1 model, for the PULP cluster cores).
    """

    def __init__(self, parent: st.Component, name: str, nb_core: int,
            nb_mutexes: int, nb_barriers: int, dispatch_size: int,
            nb_fifo_events: int, fifo_event: int, dispatch_event: int,
            mutex_event: int, barrier_event: int, wakeup_req_latency: int=6):

        super().__init__(parent, name)

        self.add_sources(['pulp/chips/magia_v3/event_unit/event_unit.cpp'])

        self.add_properties({
            'nb_core': nb_core,
            'wakeup_req_latency': wakeup_req_latency,
            'properties': {
                'dispatch': {'size': dispatch_size},
                'mutex': {'nb_mutexes': nb_mutexes},
                'barriers': {'nb_barriers': nb_barriers},
                'soc_event': {'nb_fifo_events': nb_fifo_events, 'fifo_event': fifo_event},
                'events': {
                    'dispatch': dispatch_event,
                    'mutex': mutex_event,
                    'barrier': barrier_event
                }
            }
        })

    def i_INPUT(self) -> st.SlaveItf:
        """Shared memory-mapped input (the whole event-unit register space)."""
        return st.SlaveItf(self, 'input', signature=IoV2SingleReq())

    def i_DEMUX_INPUT(self, core: int) -> st.SlaveItf:
        """Per-core demultiplexed input (private core alias of the register space)."""
        return st.SlaveItf(self, f'demux_in_{core}', signature=IoV2SingleReq())

    def i_EVENT(self, core: int, event: int) -> st.SlaveItf:
        """Hardware event input ``event`` of core ``core``."""
        return st.SlaveItf(self, f'in_event_{event}_pe_{core}', signature='wire<bool>')

    def o_IRQ_LINE(self, core: int, itf: st.SlaveItf):
        """Binds the interrupt request level of a core."""
        self.itf_bind(f'irq_line_{core}', itf, signature='wire<bool>')

    def i_IRQ_ACK(self, core: int) -> st.SlaveItf:
        """Interrupt acknowledge of a core."""
        return st.SlaveItf(self, f'irq_ack_{core}', signature='wire<int>')

    def o_CLOCK(self, core: int, itf: st.SlaveItf):
        """Binds the clock gate of a core."""
        self.itf_bind(f'clock_{core}', itf, signature='wire<bool>')
