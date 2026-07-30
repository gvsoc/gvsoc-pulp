/*
* Copyright (C) 2026 ETH Zurich, University of Bologna, and Fondazione Chips-IT
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
*
* Authors:  Alessandro Nadalini <alessandro.nadalini3@unibo.it>
*/

#include "softex.hpp"
#include <cstring>
#include <algorithm>

/**************************************************************************
* Async memory streaming engine
* A pool of `queue_depth + 1` reusable IoReq
* slots lets several beat requests be outstanding at once (instead of the
* single blocking request the previous version of this model used), so
* ACCUMULATION/DIVIDING phase duration now emerges from actual
* request/response timing against `out` rather than a hand-computed
* estimate.
**************************************************************************/

// A slot is free once its `available_at` timestamp has elapsed; scan the
// pool and grab the first slot, marking it reserved (INT64_MAX) until
// either the sync-OK path or the async response callback installs a real
// completion timestamp.
int Softex::alloc_req_slot()
{
    int64_t now = this->clock.get_cycles();
    int best_id = -1;
    int64_t best_cycle = INT64_MAX;
    for (size_t i = 0; i < this->req_slots.size(); i++)
    {
        int64_t at = this->req_slots[i].available_at;
        if (at <= now && at < best_cycle)
        {
            best_id = (int)i;
            best_cycle = at;
        }
    }
    if (best_id >= 0)
    {
        this->req_slots[best_id].available_at = INT64_MAX;
    }
    return best_id;
}

void Softex::free_req_slot(int id, int64_t available_at)
{
    this->req_slots[id].available_at = available_at;
}

// Issue-side capacity gate: outstanding requests (in flight or still
// "busy" from a sync-OK access) must stay within queue_depth.
bool Softex::issue_slot_gate_ok()
{
    int64_t now = this->clock.get_cycles();
    uint32_t outstanding = 0;
    for (auto &slot : this->req_slots)
    {
        // A slot freed this cycle (available_at == now) is already
        // reusable per alloc_req_slot()'s own "at <= now" free test; using
        // ">=" here double-counted same-cycle synchronous completions as
        // still outstanding, which deadlocks pump_issue() as soon as
        // exactly pool_size beats have been issued on a zero-latency
        // target (every slot's available_at lands on the same "now").
        if (slot.available_at > now) outstanding++;
    }
    return outstanding <= this->queue_depth;
}

void Softex::mem_response(vp::Block *__this, vp::IoReq *req)
{
    Softex *_this = (Softex *)__this;
    int slot_id = *((int *)req->arg_get(0));
    _this->handle_mem_response(slot_id, _this->clock.get_cycles());
}

// DENIED -> granted: clear the denied flag and try to keep issuing. The
// original (denied) request's own response still arrives later via
// mem_response.
void Softex::mem_grant(vp::Block *__this, vp::IoReq *req)
{
    Softex *_this = (Softex *)__this;
    _this->request_denied = false;
    _this->pump_issue();
}

void Softex::handle_mem_response(int slot_id, int64_t free_cycle)
{
    ReqSlot &slot = this->req_slots[slot_id];

    // snapshot what we need before freeing the slot, since pump_issue()
    // called at the end of this function may immediately reuse it
    uint32_t phase = slot.phase;
    uint32_t beat_idx = slot.beat_idx;
    uint32_t valid_bytes = slot.valid_bytes;
    uint8_t buf[SOFTEX_N_ROWS * 2];
    memcpy(buf, slot.buf, sizeof(buf));

    this->free_req_slot(slot_id, free_cycle);

    switch (phase)
    {
    case REQ_ACC_READ:
        this->complete_acc_beat(buf, valid_bytes, beat_idx);
        break;
    case REQ_DIV_READ:
        this->complete_div_read_beat(buf, valid_bytes, beat_idx);
        break;
    case REQ_DIV_WRITE:
        this->complete_div_write_beat(beat_idx);
        break;
    }

    // keep the pipe full
    this->pump_issue();
}

// Try to issue as many outstanding beat reads as the slot pool currently
// allows for whichever phase is active. Called on phase entry and again
// every time a slot frees up (see handle_mem_response()).
//
// Note: on a target memory that always completes synchronously with zero
// latency (IO_REQ_OK, get_latency()==0), handle_mem_response() calls back
// into pump_issue() before returning, which can issue the next beat
// straight away -- for very large vectors against such an idealized
// memory this recurses roughly once per beat. Real memory models (with
// nonzero latency, or that return IO_REQ_PENDING) complete asynchronously
// instead and don't hit this. If you need to stream extremely large
// vectors through a zero-latency test memory, consider capping
// queue_depth low or converting this into an explicit work queue drained
// from fsm ticks instead of recursive calls.
void Softex::pump_issue()
{
    if (this->request_denied)
    {
        return;
    }

    uint32_t state = this->reg_fsm_state.get();

    if (state == SOFTEX_FSM_ACCUMULATION)
    {
        while (this->beats_issued < this->n_beats && this->issue_slot_gate_ok())
        {
            // Bump beats_issued *before* issuing: on a zero-latency target,
            // issue_acc_read() completes synchronously and reenters this
            // function (via handle_mem_response()'s "keep the pipe full"
            // call) before returning here. If beats_issued were only
            // incremented after the call, that reentrant call would still
            // see the old value and reissue the very same beat forever.
            uint32_t beat_idx = this->beats_issued;
            this->beats_issued++;
            if (!this->issue_acc_read(beat_idx))
            {
                this->beats_issued--;
                break;
            }
        }
    }
    else if (state == SOFTEX_FSM_DIVIDING)
    {
        while (this->beats_issued < this->n_beats && this->issue_slot_gate_ok())
        {
            uint32_t beat_idx = this->beats_issued;
            this->beats_issued++;
            if (!this->issue_div_read(beat_idx))
            {
                this->beats_issued--;
                break;
            }
        }
    }
}

bool Softex::issue_acc_read(uint32_t beat_idx)
{
    int slot_id = this->alloc_req_slot();
    if (slot_id < 0)
    {
        return false;
    }

    ReqSlot &slot = this->req_slots[slot_id];
    uint32_t elem_size = this->job.cast_input() ? 1 : 2;
    uint32_t elems = std::min((uint32_t)SOFTEX_N_ROWS, this->num_elements - beat_idx * SOFTEX_N_ROWS);
    uint32_t bytes = elems * elem_size;
    uint32_t addr = this->job.in_addr + beat_idx * this->beat_size_in;

    slot.phase = REQ_ACC_READ;
    slot.beat_idx = beat_idx;
    slot.valid_bytes = bytes;
    memset(slot.buf, 0, sizeof(slot.buf));

    slot.req->prepare(); // preserves arg(0)/arg(1), unlike init()
    slot.req->set_addr(addr);
    slot.req->set_size(bytes);
    slot.req->set_is_write(false);
    slot.req->set_data(slot.buf);

    vp::IoReqStatus err = this->out.req(slot.req);
    if (err == vp::IO_REQ_OK)
    {
        int64_t free_cycle = this->clock.get_cycles() + (int64_t)slot.req->get_latency();
        this->handle_mem_response(slot_id, free_cycle);
    }
    else if (err == vp::IO_REQ_PENDING)
    {
        // response arrives later via mem_response()
    }
    else if (err == vp::IO_REQ_DENIED)
    {
        this->request_denied = true;
    }
    else
    {
        this->trace.fatal("Softex: unexpected IoReq status on ACC read\n");
        return false;
    }
    return true;
}

bool Softex::issue_div_read(uint32_t beat_idx)
{
    int slot_id = this->alloc_req_slot();
    if (slot_id < 0)
    {
        return false;
    }

    ReqSlot &slot = this->req_slots[slot_id];
    uint32_t elem_size = this->job.cast_input() ? 1 : 2;
    uint32_t elems = std::min((uint32_t)SOFTEX_N_ROWS, this->num_elements - beat_idx * SOFTEX_N_ROWS);
    uint32_t bytes = elems * elem_size;
    uint32_t addr = this->job.in_addr + beat_idx * this->beat_size_in;

    slot.phase = REQ_DIV_READ;
    slot.beat_idx = beat_idx;
    slot.valid_bytes = bytes;
    memset(slot.buf, 0, sizeof(slot.buf));

    slot.req->prepare();
    slot.req->set_addr(addr);
    slot.req->set_size(bytes);
    slot.req->set_is_write(false);
    slot.req->set_data(slot.buf);

    vp::IoReqStatus err = this->out.req(slot.req);
    if (err == vp::IO_REQ_OK)
    {
        int64_t free_cycle = this->clock.get_cycles() + (int64_t)slot.req->get_latency();
        this->handle_mem_response(slot_id, free_cycle);
    }
    else if (err == vp::IO_REQ_PENDING)
    {
    }
    else if (err == vp::IO_REQ_DENIED)
    {
        this->request_denied = true;
    }
    else
    {
        this->trace.fatal("Softex: unexpected IoReq status on DIV read\n");
        return false;
    }
    return true;
}

bool Softex::issue_div_write(uint32_t addr, const uint8_t *data, uint32_t bytes, uint32_t beat_idx)
{
    int slot_id = this->alloc_req_slot();
    if (slot_id < 0)
    {
        return false;
    }

    ReqSlot &slot = this->req_slots[slot_id];
    slot.phase = REQ_DIV_WRITE;
    slot.beat_idx = beat_idx;
    slot.valid_bytes = bytes;
    memcpy(slot.buf, data, bytes);

    slot.req->prepare();
    slot.req->set_addr(addr);
    slot.req->set_size(bytes);
    slot.req->set_is_write(true);
    slot.req->set_data(slot.buf);

    vp::IoReqStatus err = this->out.req(slot.req);
    if (err == vp::IO_REQ_OK)
    {
        int64_t free_cycle = this->clock.get_cycles() + (int64_t)slot.req->get_latency();
        this->handle_mem_response(slot_id, free_cycle);
    }
    else if (err == vp::IO_REQ_PENDING)
    {
    }
    else if (err == vp::IO_REQ_DENIED)
    {
        this->request_denied = true;
    }
    else
    {
        this->trace.fatal("Softex: unexpected IoReq status on DIV write\n");
        return false;
    }
    return true;
}

/**************************************************************************
* Blocking memory helpers
* Used only by the (rare, small) state-slot cache spill/fill path below
**************************************************************************/

int64_t Softex::stream_read_beat(uint32_t addr, uint8_t *buf, int size)
{
    this->mem_req->set_addr(addr);
    this->mem_req->set_data(buf);
    this->mem_req->set_size(size);
    this->mem_req->set_is_write(false);

    vp::IoReqStatus err = this->out.req(this->mem_req);
    if (err != vp::IO_REQ_OK)
    {
        this->trace.fatal("Softex: memory read error at %x (size %d)\n", addr, size);
        return 0;
    }
    return this->mem_req->get_latency();
}

int64_t Softex::stream_write_beat(uint32_t addr, uint8_t *buf, int size)
{
    this->mem_req->set_addr(addr);
    this->mem_req->set_data(buf);
    this->mem_req->set_size(size);
    this->mem_req->set_is_write(true);

    vp::IoReqStatus err = this->out.req(this->mem_req);
    if (err != vp::IO_REQ_OK)
    {
        this->trace.fatal("Softex: memory write error at %x (size %d)\n", addr, size);
        return 0;
    }
    return this->mem_req->get_latency();
}

/**************************************************************************
* State-slot cache (softex_slot_regfile.sv) simplified
* The RTL keeps N_STATE_SLOTS fully-associative slots on chip and spills
* the rest to a memory-backed cache at CACHE_BASE_ADDR. This model uses a
* direct-mapped on-chip cache instead (index = id % n_state_slots) with
* the same memory spillover idea.
**************************************************************************/
bool Softex::slot_lookup(uint32_t id, SoftexSlot &out)
{
    int idx = id % this->local_slots.size();
    SoftexSlot &s = this->local_slots[idx];
    if (s.valid && s.id == id)
    {
        out = s;
        return true;
    }
    return false;
}

void Softex::slot_spill_to_mem(SoftexSlot &victim, uint32_t cache_base_addr)
{
    uint32_t addr = cache_base_addr + victim.id * (uint32_t)this->cache_slot_size;
    uint16_t max_bits = Softex::f32_to_bf16((float)victim.maximum);
    float denom_f = (float)victim.denominator;

    uint8_t buf[6];
    memcpy(buf, &max_bits, 2);
    memcpy(buf + 2, &denom_f, 4);
    this->stream_write_beat(addr, buf, 6);
}

void Softex::slot_fill_from_mem(uint32_t id, uint32_t cache_base_addr, SoftexSlot &out)
{
    int idx = id % this->local_slots.size();
    SoftexSlot &s = this->local_slots[idx];

    if (s.valid && s.id != id)
    {
        this->slot_spill_to_mem(s, cache_base_addr);
    }

    uint32_t addr = cache_base_addr + id * (uint32_t)this->cache_slot_size;
    uint8_t buf[6] = {0};
    this->stream_read_beat(addr, buf, 6);

    uint16_t max_bits;
    float denom_f;
    memcpy(&max_bits, buf, 2);
    memcpy(&denom_f, buf + 2, 4);

    s.maximum = (double)Softex::bf16_to_f32(max_bits);
    s.denominator = (double)denom_f;
    s.valid = true;
    s.id = id;

    out = s;
}

void Softex::slot_update(uint32_t id, double maximum, double denominator, bool free_it)
{
    int idx = id % this->local_slots.size();
    SoftexSlot &s = this->local_slots[idx];

    if (s.valid && s.id != id)
    {
        // another slot occupies our line: make room for the one we're
        // about to write back
        this->slot_spill_to_mem(s, this->slot_cache_base_addr);
    }

    if (free_it)
    {
        s = SoftexSlot();
        return;
    }

    s.id = id;
    s.valid = true;
    s.maximum = maximum;
    s.denominator = denominator;
}
