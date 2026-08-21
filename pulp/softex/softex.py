'''
Copyright (C) 2026 ETH Zurich, University of Bologna, and Fondazione Chips-IT
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Authors:  Alessandro Nadalini <alessandro.nadalini3@unibo.it>
'''

import gvsoc.systree as st
import gvsoc


class Softex(st.Component):

    def __init__(self, parent, name, n_state_slots: int = 2, cache_slot_size: int = 32,
                 queue_depth: int = 8, data_width_bits: int = 96):
        """
        n_state_slots:   number of state slots kept locally in the accelerator
                          (softex_pkg::N_CTRL_STATE_SLOTS in the RTL, default 2).
                          Extra slots spill to memory at CACHE_BASE_ADDR.
        cache_slot_size: bytes reserved per slot in the memory-backed slot
                          cache (max + denominator, rounded up).
        queue_depth:     max outstanding request blocks on the memory port at
                          once (see stream_tick() in softex_stream.cpp -- one
                          block, at most data_width_bits/8 bytes, is still
                          issued per cycle regardless of this value). Higher
                          values let the model overlap more real memory
                          latency with streaming; must be >= 1.
        data_width_bits: width in bits of softex's muxed master-port datapath
                          (excludes the 32-bit strobe/aux lane); caps how many
                          bytes of a beat are moved across the interconnect
                          per cycle (see stream_tick() in softex_stream.cpp).
        """
        super(Softex, self).__init__(parent, name)

        self.set_component('pulp.softex.softex')

        self.add_properties({
            'n_state_slots': n_state_slots,
            'cache_slot_size': cache_slot_size,
            'queue_depth': queue_depth,
            'data_width_bits': data_width_bits,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        """Config/register port (memory-mapped at SOFTEX_BASE_ADD in archi_softex.h)."""
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_OUT(self, itf: gvsoc.systree.SlaveItf):
        """Data/memory port used to stream operands in/out and to spill state slots."""
        self.itf_bind('out', itf, signature='io')

    def o_IRQ(self, itf: gvsoc.systree.SlaveItf):
        """Raised when a job completes (mirrors evt_o / FINISHED in the RTL)."""
        self.itf_bind('irq', itf, signature='wire<bool>')
