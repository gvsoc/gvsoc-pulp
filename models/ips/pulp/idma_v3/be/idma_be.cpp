/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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

/*
 * Authors: Germain Haugou (germain.haugou@gmail.com)
 */

#include <algorithm>
#include <cstring>
#include "idma_be.hpp"



IdmaBackend::IdmaBackend(vp::Component *top, std::string name, const IdmaBackendParams &params,
    IdmaMeSource *me)
:   vp::Block(top, name),
    params(params),
    me(me),
    tick(this, &IdmaBackend::tick_handler),
    legalizer(this, params.width),
    r_fifo(params.num_ax_in_flight),
    w_fifo(params.num_ax_in_flight),
    wlast(params.meta_fifo_depth > 0 ? params.meta_fifo_depth
        : params.buffer_depth + params.num_ax_in_flight),
    buffer(params.width, params.buffer_depth),
    compute(params.width, params.transpose_full_duplex),
    compute_beat(params.width),
    buffer_fill(*this, "buffer_fill", 32, vp::SignalCommon::ResetKind::HighZ),
    busy(*this, "busy", 1, vp::SignalCommon::ResetKind::HighZ),
    sig_r_addr(*this, "r_addr", 32, vp::SignalCommon::ResetKind::HighZ),
    sig_r_left(*this, "r_left", 32, vp::SignalCommon::ResetKind::HighZ),
    sig_w_addr(*this, "w_addr", 32, vp::SignalCommon::ResetKind::HighZ),
    sig_w_left(*this, "w_left", 32, vp::SignalCommon::ResetKind::HighZ),
    sig_buffer_full(*this, "buffer_full", 1, vp::SignalCommon::ResetKind::HighZ),
    sig_buffer_empty(*this, "buffer_empty", 1, vp::SignalCommon::ResetKind::HighZ)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    if (params.width <= 0 || (params.width & (params.width - 1)) != 0)
    {
        this->trace.fatal("idma_v3: width must be a power of two (got %d)\n", params.width);
    }
}



void IdmaBackend::reset(bool active)
{
    if (active)
    {
        this->legalizer.reset();
        this->wake_targets.clear();
        this->r_fifo.reset();
        this->w_fifo.reset();
        this->wlast.reset();
        this->buffer.reset();
        this->compute.reset();
        this->in_order.clear();
        this->r_beat_idx = 0;
        this->w_beat_idx = 0;
        this->shown_full = false;
        this->shown_empty = false;
        this->shown_r = false;
        this->shown_w = false;
    }
}



void IdmaBackend::add_read_manager(IdmaReadManager *manager)
{
    this->read_managers[manager->protocol()] = manager;
    this->read_manager_list.push_back(manager);
}



void IdmaBackend::add_write_manager(IdmaWriteManager *manager)
{
    this->write_managers[manager->protocol()] = manager;
    this->write_manager_list.push_back(manager);
}



IdmaReadManager *IdmaBackend::read_manager(int protocol)
{
    auto it = this->read_managers.find(protocol);
    return it == this->read_managers.end() ? nullptr : it->second;
}



IdmaWriteManager *IdmaBackend::write_manager(int protocol)
{
    auto it = this->write_managers.find(protocol);
    return it == this->write_managers.end() ? nullptr : it->second;
}



// Bytes of beat beat_idx that belong to the split, in bus space: the first
// beat starts at split.offset, the last one ends at split.tailer (0 = full);
// a single-beat split applies both (idma_axi_read r_first_mask / r_last_mask,
// always one run of bytes).
IdmaBeatBytes IdmaBackend::beat_bytes(const IdmaSplit &split, int beat_idx) const
{
    int first = beat_idx == 0 ? split.offset : 0;
    int end = beat_idx == split.num_beats - 1 && split.tailer != 0 ? split.tailer
        : this->params.width;
    return IdmaBeatBytes{first, end - first};
}



bool IdmaBackend::read_beat_can_accept(IdmaReadManager *manager, void *token, int64_t now)
{
    if (!this->r_fifo.head_visible(now))
    {
        return false;
    }

    ReadSlot &slot = this->r_fifo.head();
    if (slot.manager != manager)
    {
        // Another protocol's burst is ahead in the read queue: its beats go
        // first (single r_dp_req FIFO in the RTL)
        return false;
    }
    if (slot.token != token)
    {
        this->trace.fatal("Response beat out of order (expected burst at 0x%lx)\n",
            slot.split.addr);
        return false;
    }

    return this->buffer.can_push(this->beat_bytes(slot.split, this->r_beat_idx),
        slot.split.shift, now);
}



void IdmaBackend::read_beat_accept(IdmaReadManager *manager, void *token, const uint8_t *bus,
    int64_t now)
{
    ReadSlot &slot = this->r_fifo.head();
    IdmaBeatBytes bytes = this->beat_bytes(slot.split, this->r_beat_idx);

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Read beat accepted (addr: 0x%lx, beat: %d/%d, bytes: %d+%d)\n",
        slot.split.addr, this->r_beat_idx, slot.split.num_beats, bytes.first, bytes.count);

    this->buffer.push(bytes, bus, slot.split.shift, now);
    this->buffer_fill.set(this->buffer.fill());

    if (this->r_beat_idx == 0)
    {
        IdmaWriteManager *wm = this->write_manager(slot.split.req->dst_prot);
        if (wm != nullptr && !slot.split.decouple_aw)
        {
            wm->on_read_first_beat();
        }
    }

    this->r_beat_idx++;
    if (this->r_beat_idx == slot.split.num_beats)
    {
        this->r_beat_idx = 0;
        this->r_fifo.pop(now);
    }

    // The bytes are visible to the write side next cycle
    this->wake();
}



bool IdmaBackend::write_beat_ready(IdmaWriteManager *manager, IdmaSplit **split, int *beat_idx,
    IdmaBeatBytes *bytes_out, int64_t now)
{
    if (!this->w_fifo.head_visible(now))
    {
        return false;
    }

    WriteSlot &slot = this->w_fifo.head();
    if (slot.manager != manager)
    {
        return false;
    }

    IdmaBeatBytes bytes = this->beat_bytes(slot.split, this->w_beat_idx);
    if (is_compute(slot.split))
    {
        if (!this->compute.can_pop(bytes.count, now))
        {
            return false;
        }
    }
    else if (!this->buffer.can_pop(bytes, slot.split.shift, now))
    {
        return false;
    }

    *split = &slot.split;
    *beat_idx = this->w_beat_idx;
    *bytes_out = bytes;
    return true;
}



bool IdmaBackend::write_beat_take(IdmaWriteManager *manager, uint8_t *bus, int64_t now,
    uint8_t *strb)
{
    WriteSlot &slot = this->w_fifo.head();
    IdmaBeatBytes bytes = this->beat_bytes(slot.split, this->w_beat_idx);
    bool masked = false;

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Write beat taken (addr: 0x%lx, beat: %d/%d, strb: %d+%d, compute: %d)\n",
        slot.split.addr, this->w_beat_idx, slot.split.num_beats, bytes.first, bytes.count,
        slot.split.req->compute.op);

    if (is_compute(slot.split))
    {
        // Compute transfers are beat-aligned: the output starts at lane 0
        masked = this->compute.pop(bus + bytes.first, strb ? strb + bytes.first : nullptr,
            bytes.count, now);
        if (masked && strb == nullptr)
        {
            this->trace.fatal("Masked compute output on a write manager without strobe\n");
        }
    }
    else
    {
        this->buffer.pop(bytes, slot.split.shift, bus, now);
        this->buffer_fill.set(this->buffer.fill());
        if (this->params.enable_compute)
        {
            // These bytes head the buffer: they belong to the oldest request
            InOrder &head = this->in_order.front();
            head.left -= bytes.count;
            if (head.left == 0)
            {
                this->in_order.pop_front();
            }
        }
    }

    this->w_beat_idx++;
    if (this->w_beat_idx == slot.split.num_beats)
    {
        this->w_beat_idx = 0;
        this->w_fifo.pop(now);
    }

    // Freed lanes may let a held response beat in (same cycle, see the tick)
    // and the freed FIFO slot lets the legalizer go on next cycle
    this->wake();

    return masked;
}



void IdmaBackend::compute_track(Idma1dReq *req)
{
    if (this->params.enable_compute)
    {
        this->in_order.push_back({req, req->length});
    }
}



// The engine works after the write side popped this cycle (its output
// register and pack buffers accept in the cycle they are popped), then takes
// one input beat from the buffer when the bytes heading it are a compute
// request's. A new operation is only taken by an empty engine.
bool IdmaBackend::compute_tick(int64_t now)
{
    if (!this->params.enable_compute)
    {
        return false;
    }

    bool active = this->compute.step(now);

    if (this->in_order.empty())
    {
        return active;
    }
    InOrder &head = this->in_order.front();
    const IdmaComputeCfg &cfg = head.req->compute;
    if (cfg.op == IDMA_COMPUTE_NONE)
    {
        return active;
    }
    if (cfg != this->compute.cfg())
    {
        if (!this->compute.idle())
        {
            return active;
        }
        this->compute.start(cfg);
        active |= this->compute.step(now);
    }

    int count = std::min<uint64_t>(this->params.width, head.left);
    IdmaBeatBytes bytes{0, count};
    if (!this->compute.can_accept(now) || !this->buffer.can_pop(bytes, 0, now))
    {
        return active;
    }

    uint8_t *beat = this->compute_beat.data();
    memset(beat, 0, this->params.width);
    this->buffer.pop(bytes, 0, beat, now);
    this->buffer_fill.set(this->buffer.fill());
    this->compute.push(beat, count, now);

    head.left -= count;
    if (head.left == 0)
    {
        this->in_order.pop_front();
    }

    return true;
}



void IdmaBackend::write_burst_done(IdmaWriteManager *manager, bool error)
{
    int64_t now = this->cycles();

    if (this->wlast.empty())
    {
        this->trace.fatal("Write response without an outstanding write burst\n");
        return;
    }

    LastSlot slot = this->wlast.head();
    this->wlast.pop(now);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Write burst done (last: %d, error: %d)\n",
        slot.last, error);

    if (slot.last)
    {
        this->me->complete_1d(slot.req, false);
    }

    this->wake();
}



// Ask for a tick in `cycles` cycles. The engine keeps only the earliest
// pending enqueue of an event: a request for N + 1 made while the tick is
// still pending for N would be dropped, so the target is remembered and
// re-armed by the tick itself when it runs.
void IdmaBackend::wake(int cycles)
{
    this->wake_targets.insert(this->cycles() + cycles);
    this->tick.enqueue(cycles);
}



// One cycle of the back-end. The three steps are the three combinational
// stages of the RTL that depend on each other within a cycle; everything
// else (response beats, retries) comes through callbacks that only touch
// timestamped state.
void IdmaBackend::tick_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IdmaBackend *_this = (IdmaBackend *)__this;
    int64_t now = _this->cycles();
    bool active = false;

    // The wakes for this cycle are served by this tick
    _this->wake_targets.erase(_this->wake_targets.begin(),
        _this->wake_targets.upper_bound(now));

    // 1. Legalizer: at most one read and one write split
    active |= _this->legalizer.step(now);

    // 2. Write managers: at most one data beat each, popping the buffer
    for (IdmaWriteManager *wm: _this->write_manager_list)
    {
        active |= wm->tick(now);
    }

    // 2b. Compute engine: its work, then one beat from the buffer
    active |= _this->compute_tick(now);

    // 3. Read managers: response beats held for lack of buffer room may enter
    // now that this cycle's pops were applied (push-when-full of the
    // passthrough lanes)
    for (IdmaReadManager *rm: _this->read_manager_list)
    {
        rm->retry_held_beat();
    }

    // Re-arm only while the pipeline progressed on its own this cycle:
    // entries pushed now (splits, data beats) become visible next cycle.
    // Every stall is released by an event that calls wake(): a FIFO pop, a
    // response beat, a retry, a launch.
    if (active)
    {
        _this->tick.enqueue();
    }
    // A wake asked for a later cycle while this tick was pending
    if (!_this->wake_targets.empty())
    {
        _this->tick.enqueue(*_this->wake_targets.begin() - now);
    }

    _this->busy.set(_this->is_busy());
    _this->update_signals(now);
}



void IdmaBackend::update_signals(int64_t now)
{
    if (this->legalizer.r_busy())
    {
        this->sig_r_addr.set(this->legalizer.r_addr());
        this->sig_r_left.set(this->legalizer.r_left());
    }
    else if (this->shown_r)
    {
        this->sig_r_addr.release();
        this->sig_r_left.release();
    }
    this->shown_r = this->legalizer.r_busy();
    if (this->legalizer.w_busy())
    {
        this->sig_w_addr.set(this->legalizer.w_addr());
        this->sig_w_left.set(this->legalizer.w_left());
    }
    else if (this->shown_w)
    {
        this->sig_w_addr.release();
        this->sig_w_left.release();
    }
    this->shown_w = this->legalizer.w_busy();

    // A response beat is held: no room for it in the buffer
    bool full = false;
    for (IdmaReadManager *rm: this->read_manager_list)
    {
        full |= rm->holding_beat();
    }
    if (full != this->shown_full)
    {
        if (full) this->sig_buffer_full.set(true); else this->sig_buffer_full.release();
        this->shown_full = full;
    }

    // A write burst is ready to send data which has not come in yet
    bool empty = false;
    if (this->w_fifo.head_visible(now))
    {
        WriteSlot &slot = this->w_fifo.head();
        IdmaBeatBytes bytes = this->beat_bytes(slot.split, this->w_beat_idx);
        empty = is_compute(slot.split) ? !this->compute.can_pop(bytes.count, now)
            : !this->buffer.can_pop(bytes, slot.split.shift, now);
    }
    if (empty != this->shown_empty)
    {
        if (empty) this->sig_buffer_empty.set(true); else this->sig_buffer_empty.release();
        this->shown_empty = empty;
    }
}



uint32_t IdmaBackend::busy_bits()
{
    uint32_t bits = 0;
    bool r_dp = !this->r_fifo.empty();
    bool w_dp = !this->w_fifo.empty() || !this->wlast.empty();
    for (IdmaReadManager *rm: this->read_manager_list)
    {
        r_dp |= rm->busy();
    }
    for (IdmaWriteManager *wm: this->write_manager_list)
    {
        w_dp |= wm->busy();
    }
    // {raw_coupler, eh_cnt, eh_fsm, w_leg, r_leg, w_dp, r_dp, buffer}
    bits |= !this->buffer.empty() || !this->compute.idle() ? 1 << 0 : 0;
    bits |= r_dp ? 1 << 1 : 0;
    bits |= w_dp ? 1 << 2 : 0;
    bits |= this->legalizer.r_busy() ? 1 << 3 : 0;
    bits |= this->legalizer.w_busy() ? 1 << 4 : 0;
    return bits;
}



bool IdmaBackend::is_busy()
{
    return this->busy_bits() != 0;
}
