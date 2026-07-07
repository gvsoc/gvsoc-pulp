#
# Copyright (C) 2025 ETH Zurich and University of Bologna
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

import gvsoc.systree

class L1_BankArbiter(gvsoc.systree.Component):
    """
    Requester-aware per-bank hold arbiter modelling the atomic wide RedMulE read. One instance per
    bank at the convergence (masters -> arbiter -> remove_offset -> bank), fed by all masters on two
    ports so the reserve depends on the requester:
      * `input`    -- wide RedMulE/TE reads; reserved for `hold` cycles (atomic wide read).
      * `input_pe` -- narrow PE accesses; `hold_pe` (=0), PEs were originally wired straight to the bank.
    With `hold` > the scheduler's ~1-cycle/TE de-sync, trailing TEs wait for the bank and re-sync into
    lockstep, so shared-W contention emerges; PE traffic shares the timeline (real PE<->TE
    interaction) without paying the wide-read hold. No-op when uncontended.
    """
    def __init__(self, parent: gvsoc.systree.Component, name: str, hold: int=1, hold_pe: int=1, queue_depth: int=16):
        super(L1_BankArbiter, self).__init__(parent, name)

        self.add_property('hold', hold)              # reserve per wide (TE) access = service time S
        self.add_property('hold_pe', hold_pe)        # reserve per narrow (PE) access (0 = straight to bank)
        self.add_property('queue_depth', queue_depth)  # buffered wide reads; bounds backlog to depth*S

        self.add_sources(['pulp/mempool/l1_interconnect/l1_bank_arbiter.cpp'])
