/*
 * Copyright (C) 2025 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <algorithm>

/**
 * @brief Requester-aware per-bank hold arbiter (one per bank, at masters -> arbiter -> bank).
 *
 * All masters share one reservation timeline; the reserve depends on the requester: a wide RedMulE
 * read (`input`) is atomic and holds the bank for `hold` cycles, a narrow PE access (`input_pe`)
 * adds 0 cycles (`hold_pe`, PEs were originally wired straight to the bank). With `hold` > the
 * model's ~1-cycle/TE de-sync, trailing TEs wait
 * for the bank and re-sync into lockstep, so shared-W contention emerges; PE traffic still contends
 * (delays / is delayed by TEs) without paying the wide-read hold. No-op when uncontended.
 */
class L1_BankArbiter : public vp::Component
{
public:
    L1_BankArbiter(vp::ComponentConf &conf);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);     // wide (TE)
    static vp::IoReqStatus req_pe(vp::Block *__this, vp::IoReq *req);  // narrow (PE)
    vp::IoReqStatus arbitrate(vp::IoReq *req, int64_t hold_amount);

    vp::Trace trace;

    int64_t hold;         // reserve per wide (TE) access = service time S
    int64_t hold_pe;      // reserve per narrow (PE) access (0 = originally wired straight to bank)
    int64_t queue_depth;  // buffered wide reads; bounds observable backlog to depth*S (backpressure)
    int64_t next_free = 0;   // cyclestamp at which the bank is free again
    // Per-cycle fairness snapshot: same-cycle issues see the same backlog (event order can't bias
    // who pays); the reference re-snaps after an idle gap.
    int64_t last_cycle = -1;
    int64_t cycle_base = 0;

    vp::IoSlave input_itf;
    vp::IoSlave input_pe_itf;
    vp::IoMaster output_itf;
};


L1_BankArbiter::L1_BankArbiter(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->hold = this->get_js_config()->get_int("hold");
    this->hold_pe = this->get_js_config()->get_int("hold_pe");
    this->queue_depth = this->get_js_config()->get_int("queue_depth");

    this->input_itf.set_req_meth(&L1_BankArbiter::req);
    this->new_slave_port("input", &this->input_itf);
    this->input_pe_itf.set_req_meth(&L1_BankArbiter::req_pe);
    this->new_slave_port("input_pe", &this->input_pe_itf);
    this->new_master_port("output", &this->output_itf);
}

vp::IoReqStatus L1_BankArbiter::req(vp::Block *__this, vp::IoReq *req)
{
    return ((L1_BankArbiter *)__this)->arbitrate(req, ((L1_BankArbiter *)__this)->hold);
}

vp::IoReqStatus L1_BankArbiter::req_pe(vp::Block *__this, vp::IoReq *req)
{
    return ((L1_BankArbiter *)__this)->arbitrate(req, ((L1_BankArbiter *)__this)->hold_pe);
}

vp::IoReqStatus L1_BankArbiter::arbitrate(vp::IoReq *req, int64_t hold_amount)
{
    int64_t cycles = this->clock.get_cycles();

    // Single-server queue: bank serves one access per `hold_amount` (S); backlog = next_free - cycles.
    // Re-snap the reference once per issue-cycle so same-cycle concurrent issues wait the same amount
    // (fairness) and the bank frees correctly after an idle gap.
    if (cycles != this->last_cycle)
    {
        this->cycle_base = std::max(this->next_free, cycles);
        this->last_cycle = cycles;
    }
    int64_t backlog = this->cycle_base - cycles;

    // Cap a single access's observable backlog at depth*S (finite request queue -> backpressure); the
    // surplus still accrues in next_free so the stall propagates to later accesses.
    int64_t max_backlog = this->queue_depth * hold_amount;
    int64_t wait = std::min(backlog, max_backlog);
    req->inc_latency(wait);
    this->next_free = std::max(this->next_free, cycles) + hold_amount;

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Bank queue arbitration (cycles: %ld, backlog: %ld, wait: %ld, hold: %ld, next_free: %ld)\n",
        cycles, backlog, wait, hold_amount, this->next_free);

    if (!this->output_itf.is_bound())
    {
        this->trace.fatal("L1_BankArbiter: output port is not connected\n");
        return vp::IO_REQ_INVALID;
    }

    return this->output_itf.req_forward(req);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new L1_BankArbiter(config);
}
