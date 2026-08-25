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

    this->burst_info.resize(this->burst_queue_size);
    this->burst_data.resize(this->burst_queue_size);

    for (int i = 0; i < this->burst_queue_size; i++)
    {
        BurstInfo *info = new BurstInfo();
        info->burst_id = i;
        this->burst_info[i] = info;
        this->burst_data[i] = new uint8_t[AXI_PAGE_SIZE];
    }

    // Data-less request pool (payload size 0), serving the whole-burst read
    // requests (read burst requests carry no data) and the per-issue write
    // beats (data aliases the slot's staging buffer; the downstream consumer
    // frees granted write beats, so they must be pool objects).
    this->req_allocator = vp::IoReqAllocator::get(0);
}



IDmaBeAxi::~IDmaBeAxi()
{
    for (BurstInfo *info : this->burst_info)
    {
        // Free a read request still held from an un-acked DENIED retry (nullptr
        // once freed on the burst's last response).
        if (info->read_req != nullptr)
        {
            info->read_req->free();
        }
        delete info;
    }
    if (this->held_write_beat != nullptr)
    {
        this->held_write_beat->free();
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
            if (info->read_req != nullptr)
            {
                info->read_req->free();
                info->read_req = nullptr;
            }
            info->is_write = false;
            this->free_bursts.push(info);
        }

        if (this->held_write_beat != nullptr)
        {
            this->held_write_beat->free();
            this->held_write_beat = nullptr;
        }
        this->denied_blocked = false;
    }
}



uint64_t IDmaBeAxi::get_burst_size(uint64_t base, uint64_t size)
{
    size = std::min(size, (uint64_t)AXI_PAGE_SIZE);

    // AXI limits a burst to 256 beats (AxLEN <= 255), so a burst can carry at
    // most 256 * axi_width bytes. Without this the backend would emit
    // AXI-illegal >256-beat bursts (e.g. 512 beats on a 64-bit bus for a 4 KiB
    // page), which also mis-sizes the outstanding-transaction granularity.
    size = std::min(size, (uint64_t)256 * (uint64_t)this->axi_width);

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
        // Per-issue pool beat; a beat held from a DENIED is re-used verbatim.
        // The buffer behind data (the slot's staging buffer) stays valid
        // until the burst ack — the slot is only recycled then.
        vp::IoReq *beat = this->held_write_beat != nullptr
            ? this->held_write_beat : this->req_allocator->alloc();
        this->held_write_beat = nullptr;

        beat->prepare();
        beat->set_is_write(true);
        beat->set_addr(info->base + info->bytes_issued);
        beat->set_size(beat_size);
        beat->set_data(this->burst_data[slot_idx] + info->bytes_issued);
        beat->is_first = is_first;
        beat->is_last  = is_last;
        beat->burst_id = info->burst_id;
        // Same initiator on every beat of the burst; the burst ack echoes it.
        beat->initiator = info;
        beat->set_resp_status(vp::IO_RESP_OK);

        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Sending write beat (slot: %d, addr: 0x%lx, size: 0x%lx, first: %d, last: %d)\n",
            slot_idx, beat->get_addr(), beat_size,
            is_first ? 1 : 0, is_last ? 1 : 0);

        vp::IoReqStatus status = this->bus.req(beat);
        if (status == vp::IO_REQ_DENIED)
        {
            // Hold the beat (still ours) and re-issue it on retry. Keep the
            // event-deferred retry re-issue (retry_meth -> update()): the
            // calibrated timings were pinned with it.
            this->held_write_beat = beat;
            this->denied_blocked = true;
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "Write beat denied by AXI (slot: %d)\n", slot_idx);
            return false;
        }
        if (status == vp::IO_REQ_DONE)
        {
            // Inline burst ack (only legal on the last beat, or as the
            // DONE+INVALID escape hatch). We keep the beat — recycle it.
            this->traces.assert(is_last
                    || beat->get_resp_status() == vp::IO_RESP_INVALID,
                "inline DONE on a non-last write beat without IO_RESP_INVALID");
            // No adapter in the tree annotates an inline write ack with a
            // deferred latency on this path today; completing immediately
            // keeps the accounting simple. Revisit if a native beat slave
            // starts returning annotated inline acks to the iDMA.
            this->traces.assert(beat->get_full_latency() == 0,
                "inline write burst ack carries a latency annotation");
            vp::IoRespStatus resp_status = beat->get_resp_status();
            beat->free();
            if (is_last)
            {
                info->bytes_issued += beat_size;
                this->pending_bursts.pop();
                this->complete_write_burst(info, resp_status);
                return true;
            }
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
        // Reads: exactly one full-size data-less req per burst (beat protocol:
        // the read data comes back inside distinct allocator-backed response
        // beats). We own it (initiator-owned convention) and free it on the
        // last read response. Reuse the same object across a DENIED retry.
        int slot_idx = (int)info->burst_id;
        if (info->read_req == nullptr)
        {
            info->read_req = this->req_allocator->alloc();
        }
        vp::IoReq *beat = info->read_req;

        beat->prepare();
        beat->set_is_write(false);
        beat->set_addr(info->base);
        beat->set_size(info->total_size);
        beat->set_data(NULL);
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

        // Accepted. We keep ownership of read_req (initiator-owned convention):
        // nothing downstream frees it; we free it on the last read response.
        info->bytes_issued = info->total_size;
        this->pending_bursts.pop();
        this->be->update();
        return true;
    }
}



vp::IoRespAck IDmaBeAxi::resp_meth(vp::Block *__this, vp::IoReq *req)
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

    if (info->is_write)
    {
        // The single data-less burst ack (per-burst write acknowledgement).
        // It arrives on exactly the cycle the old last per-beat ack fired,
        // so completing everything here — including all deferred source
        // acks — is timing-identical to the previous per-beat accounting.
        self->traces.assert(req->is_last && req->get_data() == nullptr,
            "malformed write burst ack (last=%d, data=%p)",
            req->is_last ? 1 : 0, req->get_data());
        vp::IoRespStatus status = req->get_resp_status();
        req->free();
        self->complete_write_burst(info, status);
        return vp::IO_RESP_ACCEPTED;
    }

    info->bytes_responded += beat_size;

    // Read beat: a distinct allocator-backed object whose co-allocated payload
    // carries the data (our read_req is data-less and never round-tripped).
    // Copy the payload into the slot's staging buffer — the forwarded chunk
    // pointer may sit in read_push_queue past this call, so it must not point
    // into the beat — then free the beat back to its pool.
    uint64_t offset = info->bytes_responded - beat_size;
    uint8_t *chunk = self->burst_data[info->burst_id] + offset;
    if (beat_size > 0)
    {
        std::memcpy(chunk, beat_data, beat_size);
    }
    req->free();

    // The downstream has already paced this at the modeled ready cycle.
    // Forward straight to the destination BE if it can take the chunk now —
    // this saves a 1-cycle fsm hop per beat and keeps the steady-state read
    // pipeline at 1 beat/cycle. When the destination is back-pressured we fall
    // back to queueing and let fsm_handler drain when it becomes ready.
    if (self->be->is_ready_to_accept_data(info->transfer))
    {
        self->read_ack_queue.push({info, beat_size});
        self->be->write_data(info->transfer, chunk, beat_size);
    }
    else
    {
        self->read_push_queue.push(std::make_tuple(info, chunk, beat_size));
        self->fsm_event.enqueue();
    }

    // Last read response received: we own read_req and free it here (nothing
    // downstream frees it). Null it so the recycled slot reallocates next time.
    if (info->bytes_responded == info->total_size)
    {
        info->read_req->free();
        info->read_req = nullptr;
    }

    return vp::IO_RESP_ACCEPTED;
}



void IDmaBeAxi::complete_write_burst(BurstInfo *info, vp::IoRespStatus status)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Write burst done (slot: %ld)\n", info->burst_id);

    // Drain the whole source-chunk FIFO in order. ack_data() on the central
    // BE frees the source's buffer AND advances the transfer's ack_size —
    // both depend on the write having really happened, which the burst ack
    // guarantees for every beat at once.
    while (!info->write_pending_acks.empty())
    {
        auto &front = info->write_pending_acks.front();
        this->be->ack_data(info->transfer, front.first, front.second);
        info->write_pending_acks.pop_front();
    }

    (void)status;
    this->free_bursts.push(info);
    this->be->update();
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
