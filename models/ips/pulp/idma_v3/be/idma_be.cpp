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
    buffer_fill(*this, "buffer_fill", 32, vp::SignalCommon::ResetKind::HighZ),
    busy(*this, "busy", 1, vp::SignalCommon::ResetKind::HighZ)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    if (params.width <= 0 || (params.width & (params.width - 1)) != 0 || params.width > 64)
    {
        this->trace.fatal("idma_v3: width must be a power of two between 1 and 64 (got %d)\n",
            params.width);
    }
}



void IdmaBackend::reset(bool active)
{
    if (active)
    {
        this->legalizer.reset();
        this->r_fifo.reset();
        this->w_fifo.reset();
        this->wlast.reset();
        this->buffer.reset();
        this->r_beat_idx = 0;
        this->w_beat_idx = 0;
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
// a single-beat split applies both (idma_axi_read r_first_mask / r_last_mask).
uint64_t IdmaBackend::beat_mask(const IdmaSplit &split, int beat_idx) const
{
    int w = this->params.width;
    uint64_t all = w == 64 ? ~(uint64_t)0 : (((uint64_t)1 << w) - 1);
    uint64_t mask = all;
    if (beat_idx == 0)
    {
        mask &= all << split.offset;
    }
    if (beat_idx == split.num_beats - 1 && split.tailer != 0)
    {
        mask &= all >> (w - split.tailer);
    }
    return mask;
}



// Bus-space mask to lane-space mask: lane i corresponds to bus byte
// (i + shift) % width, the RTL {mask, mask} >> shift.
static inline uint64_t rotate_right(uint64_t mask, int shift, int width)
{
    if (shift == 0)
    {
        return mask;
    }
    uint64_t all = width == 64 ? ~(uint64_t)0 : (((uint64_t)1 << width) - 1);
    return ((mask >> shift) | (mask << (width - shift))) & all;
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

    uint64_t mask_in = rotate_right(this->beat_mask(slot.split, this->r_beat_idx),
        slot.split.shift, this->params.width);
    return this->buffer.can_push(mask_in, now);
}



void IdmaBackend::read_beat_accept(IdmaReadManager *manager, void *token, const uint8_t *bus,
    int64_t now)
{
    ReadSlot &slot = this->r_fifo.head();
    uint64_t mask_in = rotate_right(this->beat_mask(slot.split, this->r_beat_idx),
        slot.split.shift, this->params.width);

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Read beat accepted (addr: 0x%lx, beat: %d/%d, lanes: 0x%lx)\n",
        slot.split.addr, this->r_beat_idx, slot.split.num_beats, mask_in);

    this->buffer.push(mask_in, bus, slot.split.shift, now);
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
    uint64_t *mask_out, int64_t now)
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

    uint64_t mask = this->beat_mask(slot.split, this->w_beat_idx);
    if (!this->buffer.can_pop(mask, slot.split.shift, now))
    {
        return false;
    }

    *split = &slot.split;
    *beat_idx = this->w_beat_idx;
    *mask_out = mask;
    return true;
}



void IdmaBackend::write_beat_take(IdmaWriteManager *manager, uint8_t *bus, int64_t now)
{
    WriteSlot &slot = this->w_fifo.head();
    uint64_t mask = this->beat_mask(slot.split, this->w_beat_idx);

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Write beat taken (addr: 0x%lx, beat: %d/%d, strb: 0x%lx)\n",
        slot.split.addr, this->w_beat_idx, slot.split.num_beats, mask);

    this->buffer.pop(mask, slot.split.shift, bus, now);
    this->buffer_fill.set(this->buffer.fill());

    this->w_beat_idx++;
    if (this->w_beat_idx == slot.split.num_beats)
    {
        this->w_beat_idx = 0;
        this->w_fifo.pop(now);
    }

    // Freed lanes may let a held response beat in (same cycle, see the tick)
    // and the freed FIFO slot lets the legalizer go on next cycle
    this->wake();
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
    int64_t target = this->cycles() + cycles;
    if (this->wake_target <= this->cycles() || target < this->wake_target)
    {
        this->wake_target = target;
    }
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

    // A wake requested for a later cycle while this tick was pending
    int64_t pending_wake = _this->wake_target > now ? _this->wake_target : -1;
    _this->wake_target = -1;

    // 1. Legalizer: at most one read and one write split
    active |= _this->legalizer.step(now);

    // 2. Write managers: at most one data beat each, popping the buffer
    for (IdmaWriteManager *wm: _this->write_manager_list)
    {
        active |= wm->tick(now);
    }

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
    if (pending_wake > 0)
    {
        _this->wake(pending_wake - now);
    }

    _this->busy.set(_this->is_busy());
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
    bits |= !this->buffer.empty() ? 1 << 0 : 0;
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
