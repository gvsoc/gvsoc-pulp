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
#include <vp/vp.hpp>
#include "idma_be_tcdm.hpp"



IDmaBeTcdm::IDmaBeTcdm(vp::Component *idma, std::string itf_name, IdmaBeProducer *be)
:   Block(idma, itf_name),
    fsm_event(this, &IDmaBeTcdm::fsm_handler)
{
    this->be = be;

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    idma->new_master_port(itf_name, &this->ico_itf);

    this->width = idma->get_js_config()->get_int("tcdm_width");

    this->burst_queue_maxsize = idma->get_js_config()->get_int("burst_queue_size");

    this->loc_base = idma->get_js_config()->get_int("loc_base");
}



void IDmaBeTcdm::activate_burst()
{
    if (this->current_burst_size == 0 && this->burst_queue_size.size() > 0)
    {
        this->current_burst_base = this->burst_queue_base.front();
        this->current_burst_size = this->burst_queue_size.front();
    }
}



void IDmaBeTcdm::enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer)
{
    this->burst_queue_base.push(base);
    this->burst_queue_size.push(size);
    this->burst_queue_is_write.push(is_write);
    this->burst_queue_transfer.push(transfer);

    this->activate_burst();

    this->fsm_event.enqueue();
}



void IDmaBeTcdm::read_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, false, transfer);
}



void IDmaBeTcdm::write_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, true, transfer);
}



uint64_t IDmaBeTcdm::get_burst_size(uint64_t base, uint64_t size)
{
    return size;
}


bool IDmaBeTcdm::can_accept_burst()
{
    return this->burst_queue_base.size() < this->burst_queue_maxsize;
}

bool IDmaBeTcdm::can_accept_data()
{
    // Accept a new chunk as long as:
    //   * no chunk is mid-issuance (we serialise line issuance at 1/cycle),
    //   * the ack FIFO has room (latency-deferred acks can keep accumulating
    //     without ever throttling fresh chunk arrivals — the bus, not the
    //     ack pipeline, is the rate limiter),
    //   * the downstream is not currently denying/blocking us.
    return this->write_current_chunk_size == 0
        && (int)this->write_pending_acks.size() < this->write_pending_acks_max
        && !this->granted_blocked && !this->denied_blocked;
}



void IDmaBeTcdm::write_data(IdmaTransfer *transfer, uint8_t *data, uint64_t size)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Writing data (size: 0x%lx)\n", size);

    this->write_current_chunk_ack_size = size;
    this->write_current_chunk_size = size;
    this->write_current_chunk_base = this->current_burst_base;
    this->write_current_chunk_data = data;
    this->write_current_chunk_data_start = data;
    this->write_current_transfer = transfer;

    this->write_line();
}



void IDmaBeTcdm::reset(bool active)
{
    if (active)
    {
        this->current_burst_size = 0;

        this->write_current_chunk_size = 0;
        this->write_pending_acks.clear();
        this->read_pending_pushes.clear();

        this->last_line_timestamp = -1;

        this->denied_blocked = false;
        this->granted_blocked = false;
        this->pending_line_is_write = false;
        this->pending_line_size = 0;
        this->pending_line_data = nullptr;
    }
}



void IDmaBeTcdm::write_complete_sync(int64_t /*line_latency*/ latency, uint64_t /*line_size*/ size)
{
    // write_line() has already advanced both the burst position and the
    // current-chunk cursor. If write_current_chunk_size is 0, this was the
    // last line of the chunk; queue the chunk-level ack and let the bus
    // start the next chunk next cycle. If it's not the last line, just
    // schedule the FSM to issue the next line one cycle from now —
    // throughput is one line per cycle, latency only delays acks.
    bool is_last_line = (this->write_current_chunk_size == 0);

    if (is_last_line)
    {
        int64_t ack_cycle = this->clock.get_cycles() + latency;
        this->write_pending_acks.push_back({
            this->write_current_transfer,
            this->write_current_chunk_data_start,
            this->write_current_chunk_ack_size,
            ack_cycle
        });
        // Wake the FSM when the head of the ack FIFO becomes due. If this is
        // the only entry that's the cycle we just set; otherwise the
        // existing scheduling already covers it (ClockEvent dedup keeps the
        // earlier wakeup).
        int64_t delay = ack_cycle - this->clock.get_cycles();
        this->fsm_event.enqueue(std::max(delay, (int64_t)1));
    }
    else
    {
        // Multi-line chunk: pace at one line per cycle.
        this->fsm_event.enqueue();
    }
}



void IDmaBeTcdm::read_complete_sync(int64_t latency, uint64_t size, IdmaTransfer *transfer)
{
    // pending_line_data holds the buffer the read_line() (or tcdm_response()
    // for the GRANTED path) just allocated. Park it in the per-line FIFO
    // along with its ready-cycle. The fsm_handler drains the queue in order,
    // pacing at the destination BE's accept rate.
    uint8_t *data = this->pending_line_data;
    this->pending_line_data = nullptr;

    int64_t now = this->clock.get_cycles();
    this->read_pending_pushes.push_back({
        transfer,
        data,
        size,
        now + latency
    });

    int64_t delay = latency;
    this->fsm_event.enqueue(std::max(delay, (int64_t)1));
}



void IDmaBeTcdm::write_line()
{
    // Single-shot: only one line in flight at a time, and at most one issue per cycle.
    if (this->granted_blocked || this->denied_blocked)
    {
        return;
    }

    if (this->last_line_timestamp != -1 && this->last_line_timestamp >= this->clock.get_cycles())
    {
        this->update();
        return;
    }

    this->last_line_timestamp = this->clock.get_cycles();

    uint64_t base = this->write_current_chunk_base;
    uint64_t size = this->get_line_size(this->write_current_chunk_base, this->write_current_chunk_size);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Writing line to TCDM (base: 0x%lx, size: 0x%lx)\n",
        base, size);

    vp::IoReq *req = &this->req;
    req->prepare();
    req->set_is_write(true);
    req->set_addr(base - this->loc_base);
    req->set_size(size);
    req->set_data(this->write_current_chunk_data);
    req->is_first = true;
    req->is_last = true;
    req->burst_id = -1;
    req->set_resp_status(vp::IO_RESP_OK);

    vp::IoReqStatus status = this->ico_itf.req(req);

    if (status == vp::IO_REQ_DENIED)
    {
        // Pointers untouched — the same line will be re-issued on retry.
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Write line denied by TCDM\n");
        this->denied_blocked = true;
        this->pending_line_is_write = true;
        this->pending_line_size = size;
        return;
    }

    // Accepted — advance chunk pointers AND the burst position. Advancing
    // current_burst_base/size here (rather than at ack time) is what lets
    // the next chunk's write_data() see the correct base address even while
    // this chunk's ack is still parked in write_pending_acks.
    this->write_current_chunk_base += size;
    this->write_current_chunk_size -= size;
    this->write_current_chunk_data += size;

    if (status == vp::IO_REQ_DONE)
    {
        if (req->get_resp_status() == vp::IO_RESP_INVALID)
        {
            trace.force_warning("Invalid access during TCDM write line (base: 0x%lx, size: 0x%lx)\n",
                base, size);
        }
        this->remove_chunk_from_current_burst(size);
        this->write_complete_sync(req->get_latency(), size);
        return;
    }

    // IO_REQ_GRANTED — wait for tcdm_response(). Burst position will be
    // advanced once the response fires (single in-flight in this branch).
    this->granted_blocked = true;
    this->pending_line_is_write = true;
    this->pending_line_size = size;
}



void IDmaBeTcdm::write_handle_req_ack()
{
    if (this->write_current_chunk_size == 0)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Finished TCDM line, notifying middle-end\n");

        this->be->update();
        this->be->ack_data(this->write_current_transfer, this->write_current_chunk_data_start,
            this->write_current_chunk_ack_size);
    }
    else
    {
        this->fsm_event.enqueue();
    }
}



uint64_t IDmaBeTcdm::get_line_size(uint64_t base, uint64_t size)
{
    size = std::min(size, (uint64_t)this->width);

    uint64_t next_page = (base + this->width - 1) & ~(this->width - 1);
    if (next_page > base)
    {
        size = std::min(next_page - base, size);
    }

    return size;
}



void IDmaBeTcdm::remove_chunk_from_current_burst(uint64_t size)
{
    this->current_burst_base += size;
    this->current_burst_size -= size;

    if (this->current_burst_size == 0)
    {
        this->burst_queue_base.pop();
        this->burst_queue_size.pop();
        this->burst_queue_is_write.pop();
        this->burst_queue_transfer.pop();

        this->activate_burst();

        this->be->update();
        this->update();
    }
}


void IDmaBeTcdm::read_line()
{
    if (this->granted_blocked || this->denied_blocked)
    {
        return;
    }

    // 1 line per cycle on the bus — gate so we don't issue twice in the
    // same cycle even if the FSM gets nudged from multiple paths.
    if (this->last_line_timestamp != -1
        && this->last_line_timestamp >= this->clock.get_cycles())
    {
        this->update();
        return;
    }
    this->last_line_timestamp = this->clock.get_cycles();

    vp::IoReq *req = &this->req;

    uint64_t base = this->current_burst_base;
    uint64_t size = this->get_line_size(base, this->current_burst_size);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Reading line from TCDM (base: 0x%lx, size: 0x%lx)\n",
            base, size);

    req->prepare();
    req->set_is_write(false);
    req->set_addr(base - this->loc_base);
    req->set_size(size);
    // Dynamically allocate so the buffer survives until the destination BE
    // acknowledges. Saved in pending_line_data so DENIED can free it on retry.
    uint8_t *data = new uint8_t[size];
    req->set_data(data);
    req->is_first = true;
    req->is_last = true;
    req->burst_id = -1;
    req->set_resp_status(vp::IO_RESP_OK);

    vp::IoReqStatus status = this->ico_itf.req(req);

    if (status == vp::IO_REQ_DENIED)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Read line denied by TCDM\n");
        // Free the buffer; we'll re-allocate on retry.
        delete[] data;
        this->denied_blocked = true;
        this->pending_line_is_write = false;
        this->pending_line_size = size;
        this->pending_line_data = nullptr;
        // Roll back the line-rate gate so retry can re-issue this cycle.
        this->last_line_timestamp = -1;
        return;
    }

    this->pending_line_data = data;

    if (status == vp::IO_REQ_DONE)
    {
        if (req->get_resp_status() == vp::IO_RESP_INVALID)
        {
            trace.force_warning("Invalid access during TCDM read line (base: 0x%lx, size: 0x%lx)\n",
                base, size);
        }
        // Capture transfer BEFORE remove_chunk_from_current_burst, which
        // pops burst_queue_transfer when the last line of the burst is
        // issued. read_complete_sync still needs the pointer.
        IdmaTransfer *transfer = this->burst_queue_transfer.front();
        // Advance burst position now that the line has been issued, so the
        // *next* read_line() picks up at base+size even though this line's
        // ready_cycle is still in the future.
        this->remove_chunk_from_current_burst(size);
        this->read_complete_sync(req->get_latency(), size, transfer);
        return;
    }

    // IO_REQ_GRANTED — wait for tcdm_response(). Burst position will be
    // advanced when the response actually fires (single in-flight only).
    this->granted_blocked = true;
    this->pending_line_is_write = false;
    this->pending_line_size = size;
}



void IDmaBeTcdm::write_data_ack(uint8_t *data)
{
    delete[] data;
    this->update();
}



void IDmaBeTcdm::tcdm_response(vp::Block *__this, vp::IoReq *req)
{
    IDmaBeTcdm *_this = (IDmaBeTcdm *)__this;

    _this->granted_blocked = false;

    if (_this->pending_line_is_write)
    {
        // DONE path advances the burst position inside write_line(); for the
        // GRANTED path it has to happen here, when the response really lands.
        _this->remove_chunk_from_current_burst(_this->pending_line_size);
        _this->write_complete_sync(req->get_latency(), _this->pending_line_size);
    }
    else
    {
        IdmaTransfer *transfer = _this->burst_queue_transfer.front();
        _this->remove_chunk_from_current_burst(_this->pending_line_size);
        _this->read_complete_sync(req->get_latency(), _this->pending_line_size, transfer);
    }

    _this->update();
}



void IDmaBeTcdm::tcdm_retry(vp::Block *__this)
{
    IDmaBeTcdm *_this = (IDmaBeTcdm *)__this;

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "TCDM retry — resuming issue\n");
    _this->denied_blocked = false;
    _this->update();
}



void IDmaBeTcdm::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaBeTcdm *_this = (IDmaBeTcdm *)__this;
    int64_t now = _this->clock.get_cycles();

    // Drain due chunk-acks from the FIFO. Multiple chunks can be acked in
    // the same cycle (the latency window of an earlier chunk may overlap
    // with later chunks' arrival times). Acks come out in FIFO order — the
    // bus is in-order, so each entry's cycle is >= the previous one's.
    while (!_this->write_pending_acks.empty())
    {
        auto &front = _this->write_pending_acks.front();
        if (front.cycle > now)
        {
            _this->fsm_event.enqueue(front.cycle - now);
            break;
        }
        IdmaTransfer *transfer = front.transfer;
        uint8_t *data = front.data;
        uint64_t size = front.size;
        _this->write_pending_acks.pop_front();

        _this->be->update();
        _this->be->ack_data(transfer, data, size);
    }

    // Continue the current write chunk if one is in progress (multi-line
    // chunks only — for chunk_size <= line_size, write_data() issues the
    // line inline and bytes_remaining is already 0 by the time we get here).
    if (_this->write_current_chunk_size > 0)
    {
        _this->write_line();
    }

    // Issue the next read line if the head burst is a read, the burst still
    // has data, and the FIFO has room. The 1-line-per-cycle gate lives
    // inside read_line() (last_line_timestamp).
    if (_this->burst_queue_is_write.size() > 0
        && !_this->burst_queue_is_write.front()
        && _this->current_burst_size > 0
        && (int)_this->read_pending_pushes.size() < _this->read_pending_pushes_max)
    {
        _this->read_line();
        // Stay scheduled for next cycle so we keep issuing lines as long as
        // there is work to do.
        _this->fsm_event.enqueue();
    }

    // Drain the pending-push FIFO independently of the burst queue: by the
    // time the last read's push fires, current_burst_size has already
    // decremented to 0 and the burst has been popped from
    // burst_queue_is_write — but the chunk still owes a forward to the
    // destination BE.
    while (!_this->read_pending_pushes.empty())
    {
        auto &front = _this->read_pending_pushes.front();
        if (front.ready_cycle > now)
        {
            _this->fsm_event.enqueue(front.ready_cycle - now);
            break;
        }
        if (!_this->be->is_ready_to_accept_data(front.transfer))
        {
            break;
        }
        IdmaTransfer *transfer = front.transfer;
        uint8_t *data = front.data;
        uint64_t size = front.size;
        _this->read_pending_pushes.pop_front();

        _this->be->write_data(transfer, data, size);
        _this->fsm_event.enqueue();
        break;
    }
}



void IDmaBeTcdm::update()
{
    this->fsm_event.enqueue();
}



bool IDmaBeTcdm::is_empty()
{
    return this->burst_queue_base.empty();
}
