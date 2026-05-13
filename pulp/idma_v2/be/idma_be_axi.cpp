/*
 * Copyright (C) 2024 ETH Zurich and University of Bologna
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
 * Authors: Germain Haugou, ETH Zurich (germain.haugou@iis.ee.ethz.ch)
 */

#include <algorithm>
#include <cstring>
#include <vp/vp.hpp>
#include "idma_be_axi.hpp"


// Maximum AXI burst size, also used for page crossing
#define AXI_PAGE_SIZE (1 << 12)



IDmaBeAxi::IDmaBeAxi(vp::Component *idma, std::string itf_name, IdmaBeProducer *be)
:   Block(idma, itf_name),
    fsm_event(this, &IDmaBeAxi::fsm_handler)
{
    this->be = be;

    idma->new_master_port(itf_name, &this->ico_itf, this);

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->burst_queue_size = idma->get_js_config()->get_int("burst_queue_size");
    this->burst_size = idma->get_js_config()->get_int("burst_size");
    this->axi_width = idma->get_js_config()->get_int("axi_width");
    if (this->axi_width <= 0)
    {
        this->trace.fatal("idma_v2: axi_width must be > 0\n");
    }

    // One beat per axi_width bytes, plus one extra in case the first beat is
    // unaligned and shrinks below width (very loose upper bound).
    int max_beats_per_burst = (AXI_PAGE_SIZE + this->axi_width - 1) / this->axi_width + 1;

    this->burst_info.resize(this->burst_queue_size);
    this->burst_data.resize(this->burst_queue_size);

    for (int i = 0; i < this->burst_queue_size; i++)
    {
        BurstInfo *info = new BurstInfo();
        info->burst_id = i;
        info->beats.resize(max_beats_per_burst);
        // All beats from a given slot share the same initiator handle so the
        // resp callback can recover the slot in O(1).
        for (vp::IoReq &beat : info->beats)
        {
            beat.initiator = info;
        }
        this->burst_info[i] = info;
        this->burst_data[i] = new uint8_t[AXI_PAGE_SIZE];
    }
}



IDmaBeAxi::~IDmaBeAxi()
{
    for (BurstInfo *info : this->burst_info)
    {
        delete info;
    }
    for (uint8_t *buf : this->burst_data)
    {
        delete[] buf;
    }
}



void IDmaBeAxi::reset(bool active)
{
    if (active)
    {
        while (!this->free_bursts.empty()) this->free_bursts.pop();
        while (!this->pending_bursts.empty()) this->pending_bursts.pop();
        while (!this->write_fill_queue.empty()) this->write_fill_queue.pop();
        while (!this->read_push_queue.empty()) this->read_push_queue.pop();
        while (!this->read_ack_queue.empty()) this->read_ack_queue.pop();

        for (BurstInfo *info : this->burst_info)
        {
            info->transfer = nullptr;
            info->base = 0;
            info->total_size = 0;
            info->bytes_buffered = 0;
            info->bytes_issued = 0;
            info->bytes_responded = 0;
            info->bytes_pushed = 0;
            info->bytes_acked = 0;
            info->beat_ready_cycles.clear();
            info->write_pending_acks.clear();
            info->write_bytes_source_acked = 0;
            info->next_beat_idx = 0;
            info->is_write = false;
            this->free_bursts.push(info);
        }

        this->denied_blocked = false;
    }
}



uint64_t IDmaBeAxi::get_burst_size(uint64_t base, uint64_t size)
{
    size = std::min(size, (uint64_t)AXI_PAGE_SIZE);

    if (this->burst_size > 0)
    {
        size = std::min(size, (uint64_t)this->burst_size);
    }

    uint64_t next_page = (base + AXI_PAGE_SIZE - 1) & ~(AXI_PAGE_SIZE - 1);
    if (next_page > base)
    {
        size = std::min(next_page - base, size);
    }

    return size;
}



void IDmaBeAxi::enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer)
{
    BurstInfo *info = this->free_bursts.front();
    this->free_bursts.pop();

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Enqueueing %s burst (slot: %ld, base: 0x%lx, size: 0x%lx)\n",
        is_write ? "write" : "read", info->burst_id, base, size);

    info->transfer = transfer;
    info->base = base;
    info->total_size = size;
    info->bytes_buffered = 0;
    info->bytes_issued = 0;
    info->bytes_responded = 0;
    info->bytes_pushed = 0;
    info->bytes_acked = 0;
    info->beat_ready_cycles.clear();
    info->write_pending_acks.clear();
    info->write_bytes_source_acked = 0;
    info->next_beat_idx = 0;
    info->is_write = is_write;

    this->pending_bursts.push(info);
    if (is_write)
    {
        this->write_fill_queue.push(info);
    }
    else
    {
        this->read_push_queue.push(info);
    }

    this->update();
}



void IDmaBeAxi::read_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, false, transfer);
}



void IDmaBeAxi::write_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, true, transfer);
}



bool IDmaBeAxi::issue_beat()
{
    if (this->denied_blocked || this->pending_bursts.empty())
    {
        return false;
    }

    BurstInfo *info = this->pending_bursts.front();

    // Writes can only issue beats from the bytes already buffered by
    // write_data(). Reads are gated purely by total_size.
    uint64_t limit = info->is_write ? info->bytes_buffered : info->total_size;
    if (info->bytes_issued >= limit)
    {
        return false;
    }

    uint64_t remaining = info->total_size - info->bytes_issued;
    uint64_t beat_size = std::min((uint64_t)this->axi_width, remaining);
    // For writes also clamp to the buffered prefix so we never issue a beat
    // whose data hasn't arrived yet.
    if (info->is_write)
    {
        beat_size = std::min(beat_size, info->bytes_buffered - info->bytes_issued);
    }

    bool is_first = (info->bytes_issued == 0);
    bool is_last  = (info->bytes_issued + beat_size == info->total_size);

    int slot_idx = (int)info->burst_id;
    vp::IoReq *beat = &info->beats[info->next_beat_idx];
    info->next_beat_idx++;

    beat->prepare();
    beat->set_is_write(info->is_write);
    beat->set_addr(info->base + info->bytes_issued);
    beat->set_size(beat_size);
    beat->set_data(this->burst_data[slot_idx] + info->bytes_issued);
    beat->is_first = is_first;
    beat->is_last  = is_last;
    beat->burst_id = info->burst_id;
    beat->set_resp_status(vp::IO_RESP_OK);

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Sending %s beat (slot: %d, addr: 0x%lx, size: 0x%lx, first: %d, last: %d)\n",
        info->is_write ? "write" : "read", slot_idx,
        beat->get_addr(), beat_size, is_first ? 1 : 0, is_last ? 1 : 0);

    vp::IoReqStatus status = this->ico_itf.req(beat);

    if (status == vp::IO_REQ_DENIED)
    {
        // Roll back the slot's beat-pool cursor; we'll re-issue the same beat
        // on retry. is_first/is_last/burst_id are recomputed at re-issue.
        info->next_beat_idx--;
        this->denied_blocked = true;
        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Beat denied by AXI (slot: %d)\n", slot_idx);
        return false;
    }

    // Beat accepted (either DONE inline or GRANTED for deferred resp).
    info->bytes_issued += beat_size;

    // Once every beat of this burst has been issued, pop it from the issue
    // queue. The slot stays alive until bytes_responded == total_size.
    if (info->bytes_issued == info->total_size)
    {
        this->pending_bursts.pop();
        // The central BE may now legally start a new transfer; nudge it.
        this->be->update();
    }

    if (status == vp::IO_REQ_DONE)
    {
        if (beat->get_resp_status() == vp::IO_RESP_INVALID)
        {
            this->trace.force_warning(
                "Invalid access during AXI %s beat (addr: 0x%lx, size: 0x%lx)\n",
                info->is_write ? "write" : "read",
                beat->get_addr(), beat_size);
        }
        this->handle_beat_resp(info, beat_size, beat->get_latency());
    }
    // IO_REQ_GRANTED: response will arrive later via axi_response().

    return true;
}



void IDmaBeAxi::handle_beat_resp(BurstInfo *info, uint64_t size, int64_t latency)
{
    int64_t now = this->clock.get_cycles();
    info->bytes_responded += size;

    if (info->is_write)
    {
        // Walk the source-chunk FIFO and ack every chunk whose end byte is
        // now <= bytes_responded. ack_data() on the central BE frees the
        // source's buffer AND advances the transfer's ack_size — both
        // depend on the corresponding write having really happened, so this
        // must run after the responding beat, not when the chunk was first
        // accepted by write_data().
        while (!info->write_pending_acks.empty())
        {
            auto &front = info->write_pending_acks.front();
            uint64_t chunk_end = info->write_bytes_source_acked + front.second;
            if (chunk_end > info->bytes_responded) break;

            this->be->ack_data(info->transfer, front.first, front.second);
            info->write_bytes_source_acked = chunk_end;
            info->write_pending_acks.pop_front();
        }

        if (info->bytes_responded == info->total_size)
        {
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "Write burst done (slot: %ld)\n", info->burst_id);

            this->free_bursts.push(info);
            this->be->update();
        }
        return;
    }

    // Read beat: each beat carries its own ready-cycle (response time +
    // annotated latency). Stash it in order so the FSM can release one
    // chunk downstream per ready beat.
    info->beat_ready_cycles.push_back(now + latency);

    // Schedule the FSM as soon as the head of the deque is ready.
    int64_t head_ready = info->beat_ready_cycles.front();
    int64_t delay = head_ready - now;
    this->fsm_event.enqueue(std::max(delay, (int64_t)1));
}



void IDmaBeAxi::axi_response(vp::Block *__this, vp::IoReq *req)
{
    IDmaBeAxi *_this = (IDmaBeAxi *)__this;
    BurstInfo *info = (BurstInfo *)req->initiator;

    if (req->get_resp_status() == vp::IO_RESP_INVALID)
    {
        _this->trace.force_warning(
            "Invalid access during AXI %s beat (addr: 0x%lx, size: 0x%lx)\n",
            info->is_write ? "write" : "read", req->get_addr(), req->get_size());
    }

    _this->handle_beat_resp(info, req->get_size(), req->get_latency());
}



void IDmaBeAxi::axi_retry(vp::Block *__this)
{
    IDmaBeAxi *_this = (IDmaBeAxi *)__this;

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "AXI retry — resuming issue\n");
    _this->denied_blocked = false;
    _this->update();
}



void IDmaBeAxi::write_data(IdmaTransfer *transfer, uint8_t *data, uint64_t size)
{
    // Fill the head write burst with the chunk just delivered by the source
    // backend. The chunk may span more than the current burst's remaining
    // size (the central BE does not split chunks at burst boundaries), so
    // walk through write_fill_queue copying into successive slot buffers
    // and recording the chunk pointer for later ack.
    uint64_t remaining = size;
    uint8_t *src = data;

    while (remaining > 0)
    {
        if (this->write_fill_queue.empty())
        {
            this->trace.fatal(
                "write_data(): no write burst available to absorb %lu bytes\n",
                (unsigned long)remaining);
            return;
        }

        BurstInfo *info = this->write_fill_queue.front();
        uint64_t room = info->total_size - info->bytes_buffered;
        uint64_t take = std::min(room, remaining);

        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Filling write burst (slot: %ld, offset: 0x%lx, size: 0x%lx)\n",
            info->burst_id, info->bytes_buffered, take);

        std::memcpy(this->burst_data[info->burst_id] + info->bytes_buffered, src, take);
        info->bytes_buffered += take;
        // Record the source pointer + this slot's share of it so handle_beat_resp
        // can return ownership to the source as the write beats are responded.
        // In practice source chunks (e.g. TCDM lines) never straddle a burst
        // boundary, so `take == size` on the first iteration and this entry
        // covers the whole chunk.
        info->write_pending_acks.push_back({src, take});
        src += take;
        remaining -= take;

        if (info->bytes_buffered == info->total_size)
        {
            this->write_fill_queue.pop();
        }
    }

    this->update();
}



void IDmaBeAxi::write_data_ack(uint8_t *data)
{
    // Read path: the destination BE has consumed the chunk at the head of
    // read_ack_queue. Pop it, charge its size to its slot's bytes_acked, and
    // recycle the slot when the whole burst has been acked.
    if (this->read_ack_queue.empty())
    {
        return;
    }

    auto entry = this->read_ack_queue.front();
    this->read_ack_queue.pop();
    BurstInfo *info = entry.first;
    uint64_t chunk = entry.second;

    info->bytes_acked += chunk;

    if (info->bytes_acked == info->total_size)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Acknowledged read burst (slot: %ld)\n", info->burst_id);
        this->free_bursts.push(info);
        this->be->update();
    }

    this->fsm_event.enqueue();
}



bool IDmaBeAxi::can_accept_burst()
{
    return !this->free_bursts.empty();
}



bool IDmaBeAxi::can_accept_data()
{
    // The slot buffer is sized to the maximum legal burst, so any in-flight
    // write burst can always take more bytes. Accept as long as there is an
    // unfilled write burst at the head of the fill queue.
    return !this->write_fill_queue.empty();
}



void IDmaBeAxi::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaBeAxi *_this = (IDmaBeAxi *)__this;

    // 1. Issue one beat per cycle, as long as something is pending and the
    //    downstream isn't currently denying us.
    if (_this->issue_beat())
    {
        _this->fsm_event.enqueue();
    }

    // 2. Forward one read chunk to the destination BE per cycle, as long as
    //    the next beat's per-beat ready cycle has elapsed and the
    //    destination is ready. The head of `beat_ready_cycles` belongs to
    //    the chunk being forwarded right now.
    while (!_this->read_push_queue.empty())
    {
        BurstInfo *info = _this->read_push_queue.front();
        int64_t now = _this->clock.get_cycles();

        if (info->beat_ready_cycles.empty())
        {
            // No beat response in the queue yet.
            break;
        }
        int64_t ready = info->beat_ready_cycles.front();
        if (ready > now)
        {
            _this->fsm_event.enqueue(ready - now);
            break;
        }
        if (!_this->be->is_ready_to_accept_data(info->transfer))
        {
            break;
        }

        // Chunk size: matches the beat that just became ready. We know its
        // size implicitly from the slot's geometry — the beat boundary is at
        // bytes_pushed (start) and bytes_pushed + min(axi_width,
        // total_size - bytes_pushed) (end).
        uint64_t remaining = info->total_size - info->bytes_pushed;
        uint64_t chunk = std::min(remaining, (uint64_t)_this->axi_width);
        uint8_t *data = _this->burst_data[info->burst_id] + info->bytes_pushed;

        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Forwarding read chunk (slot: %ld, offset: 0x%lx, size: 0x%lx)\n",
            info->burst_id, info->bytes_pushed, chunk);

        info->beat_ready_cycles.pop_front();
        _this->read_ack_queue.push({info, chunk});
        info->bytes_pushed += chunk;

        bool finished = (info->bytes_pushed == info->total_size);
        if (finished)
        {
            _this->read_push_queue.pop();
        }

        _this->be->write_data(info->transfer, data, chunk);

        // Pace at one chunk per cycle.
        _this->fsm_event.enqueue();
        break;
    }
}



void IDmaBeAxi::update()
{
    this->fsm_event.enqueue();
}



bool IDmaBeAxi::is_empty()
{
    // The legacy contract: "is this backend currently doing nothing for any
    // burst it owns?" The central BE uses this to gate switching source
    // backends. A burst is owned by us from enqueue_burst() until the slot
    // returns to free_bursts; equivalently, until every queue is empty.
    return this->pending_bursts.empty()
        && this->write_fill_queue.empty()
        && this->read_push_queue.empty()
        && this->read_ack_queue.empty();
}
