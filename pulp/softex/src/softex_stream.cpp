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
* Memory streaming engine (ACCUMULATION / DIVIDING)
* See the block comment above the member declarations in softex.hpp.
**************************************************************************/

// See SoftexAddr::CLUSTER_WINDOW_MASK / this method's declaration in
// softex.hpp for why this exists.
uint32_t Softex::local_addr(uint32_t addr) const
{
    return addr & SoftexAddr::CLUSTER_WINDOW_MASK;
}

// See this method's declaration in softex.hpp for why this exists. Splits
// [addr, addr+size) into chunks that never cross a 4-byte (GRANULE)
// boundary, issuing one `out.req()` per chunk on the caller-supplied `req`
// object, and (if requested) reports the max latency seen across those
// chunks. Several of them land in the same cycle across the L1
// interleaver's parallel banks, so the group's cost is their max, not
// their sum.
// PENDING/DENIED on the very first chunk (nothing transferred yet)
// is propagated exactly like a single unsplit request would be; a later
// chunk resolving to anything but OK is a hard fatal.
vp::IoReqStatus Softex::req_split(vp::IoReq *req, uint32_t addr, uint8_t *buf, uint32_t size, bool is_write,
                                   int64_t *out_max_latency)
{
    constexpr uint32_t GRANULE = 4; // L1 interleaver granule (interleaving_bits=2)
    uint32_t done = 0;
    int64_t max_latency = 0;
    while (done < size)
    {
        uint32_t chunk = std::min(size - done, GRANULE - ((addr + done) % GRANULE));

        req->prepare();
        req->set_addr(addr + done);
        req->set_size(chunk);
        req->set_is_write(is_write);
        req->set_data(buf + done);

        vp::IoReqStatus err = this->out.req(req);
        if (err != vp::IO_REQ_OK)
        {
            if (done == 0)
            {
                return err;
            }
            this->trace.fatal("Softex: unexpected IoReq status mid-beat while splitting a request across L1 banks\n");
            return err;
        }

        int64_t latency = (int64_t)req->get_latency();
        if (latency > max_latency)
        {
            max_latency = latency;
        }

        done += chunk;
    }
    if (out_max_latency != nullptr)
    {
        *out_max_latency = max_latency;
    }
    return vp::IO_REQ_OK;
}

// Set up cur_beat_* to (re-)start streaming beat `beat_idx`. For
// ACCUMULATION and the read half of DIVIDING this means reading
// job.in_addr's bytes for that beat; DIVIDING's write half is instead set
// up directly by stream_advance_beat() once the read+compute is done.
void Softex::stream_start_beat(uint32_t beat_idx)
{
    this->cur_beat_idx = beat_idx;

    uint32_t elem_size = this->job.cast_input() ? 1 : 2;
    uint32_t elems = std::min((uint32_t)SOFTEX_N_ROWS, this->num_elements - beat_idx * SOFTEX_N_ROWS);

    this->cur_beat_bytes = elems * elem_size;
    this->cur_beat_bytes_sent = 0;
    this->cur_beat_is_write = false;
    this->cur_beat_base_addr = this->local_addr(this->job.in_addr + beat_idx * this->beat_size_in);
    memset(this->cur_beat_buf, 0, sizeof(this->cur_beat_buf));
}

// Called once the beat/direction that stream_tick() was streaming has had
// all of its bytes both sent and their responses retired (pending_req_queue
// empty). Applies that beat's numerics then either moves on to the next 
// beat/direction or, once the whole phase is done, hands off to the matching
// control state exactly like the old beats_completed check did.
void Softex::stream_advance_beat()
{
    uint32_t state = this->reg_fsm_state.get();

    if (state == SOFTEX_FSM_ACCUMULATION)
    {
        this->complete_acc_beat(this->cur_beat_buf, this->cur_beat_bytes, this->cur_beat_idx);

        this->beats_completed++;
        if (this->beats_completed == this->n_beats)
        {
            // approximate pipeline fill+drain, charged once the stream is done
            this->reg_fsm_state.set(SOFTEX_FSM_WAIT_DATAPATH_EMPTY);
            this->trace_phase("WAIT_DATAPATH_EMPTY");
            this->enqueue_fsm(SoftexLatency::ACC_FILL);
            return;
        }
        this->stream_start_beat(this->cur_beat_idx + 1);
        return;
    }

    // SOFTEX_FSM_DIVIDING
    if (!this->cur_beat_is_write)
    {
        // Finished reading+normalizing this beat's input; queue its
        // computed output bytes for the (shared, muxed) port's write half.
        uint32_t out_bytes = this->complete_div_read_beat(this->cur_beat_buf, this->cur_beat_bytes,
                                                            this->cur_beat_idx, this->cur_beat_outbuf);
        this->cur_beat_is_write = true;
        this->cur_beat_bytes = out_bytes;
        this->cur_beat_bytes_sent = 0;
        this->cur_beat_base_addr = this->local_addr(this->job.out_addr + this->cur_beat_idx * this->beat_size_out);
        return;
    }

    // Finished writing this beat's output.
    this->beats_completed++;
    if (this->beats_completed == this->n_beats)
    {
        // softex_ctrl.sv's DIVIDING transitions straight to FINISHED on
        // out_stream_flags_i.done, no separate drain state, and
        // FINISHED itself is unconditionally 1 cycle; see CTRL_LAT.
        this->reg_fsm_state.set(SOFTEX_FSM_FINISHED);
        this->trace_phase("FINISHED");
        this->enqueue_fsm(SoftexLatency::CTRL_LAT);
        return;
    }
    this->stream_start_beat(this->cur_beat_idx + 1);
}

// One cycle's worth of ACCUMULATION/DIVIDING streaming work. Ticks every
// cycle (re-enqueues fsm_event with delay 1) for as long as either state
// stays active. Modeled directly on light_redmule::fsm_handler's
// PRELOAD/ROUTINE/STORING cases:
//   - Send: issue at most one SoftexBus::WIDTH_BYTES-sized block this cycle
//   - Receive: retire any in-flight blocks whose latency has elapsed.
//   - Once a beat/direction's bytes are all sent and retired, apply its
//     numerics and move on (stream_advance_beat()).
void Softex::stream_tick()
{
    int64_t now = this->clock.get_cycles();

    if (this->cur_beat_bytes_sent < this->cur_beat_bytes &&
        (uint32_t)this->pending_req_queue.size() <= this->queue_depth)
    {
        uint32_t chunk = std::min((uint32_t)SoftexBus::WIDTH_BYTES,
                                   this->cur_beat_bytes - this->cur_beat_bytes_sent);
        uint8_t *buf_ptr = (this->cur_beat_is_write ? this->cur_beat_outbuf : this->cur_beat_buf)
                           + this->cur_beat_bytes_sent;
        uint32_t addr = this->cur_beat_base_addr + this->cur_beat_bytes_sent;

        int64_t max_latency = 0;
        vp::IoReqStatus err = this->req_split(this->stream_req, addr, buf_ptr, chunk,
                                               this->cur_beat_is_write, &max_latency);
        if (err != vp::IO_REQ_OK)
        {
            // Softex's TCDM port is assumed to always resolve synchronously
            // PENDING/DENIED would mean it's wired to a target that doesn't 
            // match that assumption.
            this->trace.fatal("Softex: unexpected IoReq status while streaming\n");
            return;
        }

        this->pending_req_queue.push(now + max_latency);
        this->cur_beat_bytes_sent += chunk;
    }

    while (!this->pending_req_queue.empty() && this->pending_req_queue.front() <= now)
    {
        this->pending_req_queue.pop();
    }

    if (this->cur_beat_bytes_sent >= this->cur_beat_bytes && this->pending_req_queue.empty())
    {
        this->stream_advance_beat();

        uint32_t state = this->reg_fsm_state.get();
        if (state != SOFTEX_FSM_ACCUMULATION && state != SOFTEX_FSM_DIVIDING)
        {
            // stream_advance_beat() moved on to a control state and already
            // called enqueue_fsm() itself -- don't also tick next cycle.
            return;
        }
    }

    this->enqueue_fsm(1);
}

/**************************************************************************
* Blocking memory helpers
* Used only by the (rare, small) state-slot cache spill/fill path below
**************************************************************************/

int64_t Softex::stream_read_beat(uint32_t addr, uint8_t *buf, int size)
{
    int64_t max_latency = 0;
    vp::IoReqStatus err = this->req_split(this->mem_req, addr, buf, (uint32_t)size, false, &max_latency);
    if (err != vp::IO_REQ_OK)
    {
        this->trace.fatal("Softex: memory read error at %x (size %d)\n", addr, size);
        return 0;
    }
    return max_latency;
}

int64_t Softex::stream_write_beat(uint32_t addr, uint8_t *buf, int size)
{
    int64_t max_latency = 0;
    vp::IoReqStatus err = this->req_split(this->mem_req, addr, buf, (uint32_t)size, true, &max_latency);
    if (err != vp::IO_REQ_OK)
    {
        this->trace.fatal("Softex: memory write error at %x (size %d)\n", addr, size);
        return 0;
    }
    return max_latency;
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
    uint32_t addr = this->local_addr(cache_base_addr + victim.id * (uint32_t)this->cache_slot_size);
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

    uint32_t addr = this->local_addr(cache_base_addr + id * (uint32_t)this->cache_slot_size);
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
