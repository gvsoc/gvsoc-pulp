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
    bus(&IDmaBeAxi::retry_meth, &IDmaBeAxi::resp_meth),
    fsm_event(this, &IDmaBeAxi::fsm_handler)
{
    this->be = be;

    // The owning component exposes the bus-facing master under `itf_name`,
    // with `this` (the IDmaBeAxi block) as the callback context so the
    // static resp_meth / retry_meth dispatch directly to us. The Python
    // generator declares signature IoV2Beat on this port, so the framework
    // will auto-insert an IoV2BeatAdapter downstream when the bound slave
    // declares IoV2BigPacket.
    idma->new_master_port(itf_name, &this->bus, this);

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->burst_queue_size = idma->get_js_config()->get_int("burst_queue_size");
    this->burst_size = idma->get_js_config()->get_int("burst_size");
    this->axi_width = idma->get_js_config()->get_int("axi_width");
    if (this->axi_width <= 0)
    {
        this->trace.fatal("idma_v2: axi_width must be > 0\n");
    }

    // One beat per axi_width bytes (writes), plus one extra in case the first
    // beat is unaligned and shrinks below width. Reads only ever consume
    // beats[0] (a single full-size req per burst).
    int max_beats_per_burst = (AXI_PAGE_SIZE + this->axi_width - 1) / this->axi_width + 1;

    this->burst_info.resize(this->burst_queue_size);
    this->burst_data.resize(this->burst_queue_size);

    for (int i = 0; i < this->burst_queue_size; i++)
    {
        BurstInfo *info = new BurstInfo();
        info->burst_id = i;
        info->beats.resize(max_beats_per_burst);
        // All beats from a given slot share the same initiator handle so the
        // resp_meth callback can recover the slot in O(1).
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
        // Free a read request still held from an un-acked DENIED retry (nullptr
        // once the bus accepted it and took ownership).
        delete info->read_req;
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
            info->bytes_acked = 0;
            info->write_pending_acks.clear();
            info->write_bytes_source_acked = 0;
            info->next_beat_idx = 0;
            if (info->read_req != nullptr)
            {
                delete info->read_req;
                info->read_req = nullptr;
            }
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
    info->bytes_acked = 0;
    info->write_pending_acks.clear();
    info->write_bytes_source_acked = 0;
    info->next_beat_idx = 0;
    info->is_write = is_write;

    this->pending_bursts.push(info);
    if (is_write)
    {
        this->write_fill_queue.push(info);
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

    if (info->is_write)
    {
        // Writes can only issue beats from the bytes already buffered by
        // write_data(). Walk one axi_width-sized beat per call.
        uint64_t limit = info->bytes_buffered;
        if (info->bytes_issued >= limit)
        {
            return false;
        }

        uint64_t remaining = info->total_size - info->bytes_issued;
        uint64_t beat_size = std::min((uint64_t)this->axi_width, remaining);
        beat_size = std::min(beat_size, info->bytes_buffered - info->bytes_issued);

        bool is_first = (info->bytes_issued == 0);
        bool is_last  = (info->bytes_issued + beat_size == info->total_size);

        int slot_idx = (int)info->burst_id;
        vp::IoReq *beat = &info->beats[info->next_beat_idx];
        info->next_beat_idx++;

        beat->prepare();
        beat->set_is_write(true);
        beat->set_addr(info->base + info->bytes_issued);
        beat->set_size(beat_size);
        beat->set_data(this->burst_data[slot_idx] + info->bytes_issued);
        beat->is_first = is_first;
        beat->is_last  = is_last;
        beat->burst_id = info->burst_id;
        beat->set_resp_status(vp::IO_RESP_OK);

        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Sending write beat (slot: %d, addr: 0x%lx, size: 0x%lx, first: %d, last: %d)\n",
            slot_idx, beat->get_addr(), beat_size,
            is_first ? 1 : 0, is_last ? 1 : 0);

        vp::IoReqStatus status = this->bus.req(beat);
        // An IoV2Beat master never surfaces IO_REQ_DONE: an auto-inserted
        // IoV2BeatAdapter converts inline DONE into scheduled beat callbacks,
        // and a directly-bound IoV2Beat slave responds asynchronously per beat.
        if (status == vp::IO_REQ_DENIED)
        {
            // Roll back the slot's beat-pool cursor; we'll re-issue the same
            // beat on retry. The downstream has not taken the request.
            info->next_beat_idx--;
            this->denied_blocked = true;
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "Write beat denied by AXI (slot: %d)\n", slot_idx);
            return false;
        }

        info->bytes_issued += beat_size;
        if (info->bytes_issued == info->total_size)
        {
            this->pending_bursts.pop();
            // The central BE may now legally start a new transfer; nudge it.
            this->be->update();
        }
        return true;
    }
    else
    {
        // Reads: exactly one full-size req per burst, on a heap-allocated
        // request the consuming side owns and frees (no pooled object handed
        // out). Reuse the same object across a DENIED retry; clear the handle
        // once the bus accepts it.
        int slot_idx = (int)info->burst_id;
        if (info->read_req == nullptr)
        {
            info->read_req = new vp::IoReq();
        }
        vp::IoReq *beat = info->read_req;

        beat->prepare();
        beat->set_is_write(false);
        beat->set_addr(info->base);
        beat->set_size(info->total_size);
        beat->set_data(this->burst_data[slot_idx]);
        beat->is_first = true;
        beat->is_last  = true;
        beat->burst_id = info->burst_id;
        beat->initiator = info;
        beat->set_resp_status(vp::IO_RESP_OK);

        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Sending read burst (slot: %d, addr: 0x%lx, size: 0x%lx)\n",
            slot_idx, info->base, info->total_size);

        vp::IoReqStatus status = this->bus.req(beat);
        if (status == vp::IO_REQ_DENIED)
        {
            this->denied_blocked = true;
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "Read burst denied by AXI (slot: %d)\n", slot_idx);
            return false;   // keep info->read_req for the retry
        }

        // Accepted: the bus side (slave / beat adapter) owns the request now.
        info->read_req = nullptr;
        info->bytes_issued = info->total_size;
        this->pending_bursts.pop();
        this->be->update();
        return true;
    }
}



void IDmaBeAxi::resp_meth(vp::Block *__this, vp::IoReq *req)
{
    auto *self = (IDmaBeAxi *)__this;
    BurstInfo *info = (BurstInfo *)req->initiator;
    uint64_t beat_size = req->get_size();
    uint8_t *beat_data = req->get_data();

    if (req->get_resp_status() == vp::IO_RESP_INVALID)
    {
        self->trace.force_warning(
            "Invalid access during AXI %s beat (slot: %ld, addr: 0x%lx, size: 0x%lx)\n",
            info->is_write ? "write" : "read",
            info->burst_id, req->get_addr(), beat_size);
    }

    info->bytes_responded += beat_size;

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

            self->be->ack_data(info->transfer, front.first, front.second);
            info->write_bytes_source_acked = chunk_end;
            info->write_pending_acks.pop_front();
        }

        if (info->bytes_responded == info->total_size)
        {
            self->trace.msg(vp::Trace::LEVEL_TRACE,
                "Write burst done (slot: %ld)\n", info->burst_id);
            self->free_bursts.push(info);
            self->be->update();
        }
        return;
    }

    // Read beat. The downstream has already paced this at the modeled ready
    // cycle. Forward straight to the destination BE if it can take the
    // chunk now — this saves a 1-cycle fsm hop per beat and keeps the
    // steady-state read pipeline at 1 beat/cycle. When the destination is
    // back-pressured we fall back to queueing and let fsm_handler drain
    // when it becomes ready.
    if (self->be->is_ready_to_accept_data(info->transfer))
    {
        self->read_ack_queue.push({info, beat_size});
        self->be->write_data(info->transfer, beat_data, beat_size);
    }
    else
    {
        self->read_push_queue.push(std::make_tuple(info, beat_data, beat_size));
        self->fsm_event.enqueue();
    }

    // Free the read response beat now that its bytes have been forwarded (the
    // data lives in burst_data, not in the beat). Every read beat is a freeable
    // object: a multi-beat read delivers adapter-allocated beats, and a
    // single-beat read round-trips our own heap read_req — both are ours to
    // delete here. (The write path returns above; its acks ride the pooled
    // beats[] slots and must not be freed.)
    delete req;
}



void IDmaBeAxi::retry_meth(vp::Block *__this, vp::IoRetryChannel)
{
    auto *self = (IDmaBeAxi *)__this;
    self->trace.msg(vp::Trace::LEVEL_TRACE, "AXI retry — resuming issue\n");
    self->denied_blocked = false;
    self->update();
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
        // Record the source pointer + this slot's share of it so resp_meth
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

    // 1. Issue beats while there is something pending and the downstream
    //    isn't denying us. One issue per fsm tick (reads issue full-size
    //    once; writes pace at one axi_width-sized beat per cycle).
    if (_this->issue_beat())
    {
        _this->fsm_event.enqueue();
    }

    // 2. Forward one read chunk to the destination BE per cycle. The adapter
    //    has already paced beat arrivals at one per cycle, so the queue
    //    naturally throttles itself — we only stall here when the
    //    destination BE isn't ready.
    if (!_this->read_push_queue.empty())
    {
        auto &front = _this->read_push_queue.front();
        BurstInfo *info = std::get<0>(front);
        uint8_t *data  = std::get<1>(front);
        uint64_t size  = std::get<2>(front);

        if (_this->be->is_ready_to_accept_data(info->transfer))
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "Forwarding read chunk (slot: %ld, size: 0x%lx)\n",
                info->burst_id, size);

            _this->read_ack_queue.push({info, size});
            _this->read_push_queue.pop();
            _this->be->write_data(info->transfer, data, size);
            // Stay scheduled in case more chunks are queued.
            _this->fsm_event.enqueue();
        }
        // If BE not ready, leave the queue alone — be->update() will nudge us
        // when it becomes ready.
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
