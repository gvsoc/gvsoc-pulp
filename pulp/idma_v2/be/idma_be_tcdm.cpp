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
    return this->write_current_chunk_size == 0 && this->write_ack_timestamp == -1
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
        this->read_pending_line_size = 0;

        this->write_current_chunk_size = 0;
        this->write_ack_timestamp = -1;

        this->last_line_timestamp = -1;

        this->denied_blocked = false;
        this->granted_blocked = false;
        this->pending_line_is_write = false;
        this->pending_line_size = 0;
        this->pending_line_data = nullptr;
    }
}



void IDmaBeTcdm::write_complete_sync(int64_t latency, uint64_t size)
{
    if (latency == 0)
    {
        // Inline path: ack the chunk and chain.
        this->remove_chunk_from_current_burst(size);
        this->write_handle_req_ack();
    }
    else
    {
        // Deferred ack via the existing wall-clock machinery.
        this->write_ack_timestamp = this->clock.get_cycles() + latency;
        this->write_ack_size = size;
        this->fsm_event.enqueue(latency);
    }
}



void IDmaBeTcdm::read_complete_sync(int64_t latency, uint64_t size)
{
    // pending_line_data holds the buffer just allocated for this line.
    uint8_t *data = this->pending_line_data;
    this->pending_line_data = nullptr;

    if (latency == 0 && this->be->is_ready_to_accept_data(this->burst_queue_transfer.front()))
    {
        IdmaTransfer *transfer = this->burst_queue_transfer.front();
        this->remove_chunk_from_current_burst(size);
        this->be->write_data(transfer, data, size);
    }
    else
    {
        this->read_pending_timestamp = this->clock.get_cycles() + latency;
        this->read_pending_line_data = data;
        this->read_pending_line_size = size;
        this->fsm_event.enqueue(latency);
    }
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

    // Accepted — advance chunk pointers.
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
        this->write_complete_sync(req->get_latency(), size);
        return;
    }

    // IO_REQ_GRANTED — wait for tcdm_response().
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
        this->read_complete_sync(req->get_latency(), size);
        return;
    }

    // IO_REQ_GRANTED — wait for tcdm_response().
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
        _this->write_complete_sync(req->get_latency(), _this->pending_line_size);
    }
    else
    {
        _this->read_complete_sync(req->get_latency(), _this->pending_line_size);
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

    // Pending write ack timer.
    if (_this->write_ack_timestamp != -1)
    {
        if (_this->write_ack_timestamp <= _this->clock.get_cycles())
        {
            _this->write_ack_timestamp = -1;
            _this->remove_chunk_from_current_burst(_this->write_ack_size);
            _this->write_handle_req_ack();
        }
        else
        {
            _this->fsm_event.enqueue(_this->write_ack_timestamp - _this->clock.get_cycles());
        }
    }

    // Continue the current write chunk if one is in progress.
    if (_this->write_current_chunk_size > 0 && _this->write_ack_timestamp == -1)
    {
        _this->write_line();
    }

    // Read burst processing.
    if (_this->burst_queue_is_write.size() > 0 && !_this->burst_queue_is_write.front())
    {
        if (_this->current_burst_size > 0 && _this->read_pending_line_size == 0)
        {
            _this->read_line();
        }

        if (_this->read_pending_line_size > 0
            && _this->be->is_ready_to_accept_data(_this->burst_queue_transfer.front()))
        {
            if (_this->read_pending_timestamp <= _this->clock.get_cycles())
            {
                uint64_t size = _this->read_pending_line_size;
                _this->read_pending_line_size = 0;
                IdmaTransfer *transfer = _this->burst_queue_transfer.front();
                _this->remove_chunk_from_current_burst(size);
                _this->be->write_data(transfer, _this->read_pending_line_data, size);
            }
            else
            {
                _this->fsm_event.enqueue(_this->read_pending_timestamp - _this->clock.get_cycles());
            }
        }
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
