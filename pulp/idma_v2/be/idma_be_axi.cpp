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

    int burst_queue_size = idma->get_js_config()->get_int("burst_queue_size");

    this->burst_size = idma->get_js_config()->get_int("burst_size");

    this->bursts.resize(burst_queue_size);
    this->burst_info.resize(burst_queue_size);

    for (int i = 0; i < burst_queue_size; i++)
    {
        // Permanent sidecar pointer: anywhere we have an IoReq* coming back
        // through resp(), `(BurstInfo *)req->initiator` recovers the slot.
        this->bursts[i].initiator = &this->burst_info[i];
        this->bursts[i].set_data(new uint8_t[AXI_PAGE_SIZE]);
    }
}



IDmaBeAxi::~IDmaBeAxi()
{
    for (vp::IoReq &req: this->bursts)
    {
        delete[] req.get_data();
    }
}



void IDmaBeAxi::reset(bool active)
{
    if (active)
    {
        while(this->free_bursts.size() > 0)
        {
            this->free_bursts.pop();
        }
        while(this->read_waiting_bursts.size() > 0)
        {
            this->read_waiting_bursts.pop();
        }
        while(this->pending_bursts.size() > 0)
        {
            this->pending_bursts.pop();
        }

        for (vp::IoReq &req: this->bursts)
        {
            this->free_bursts.push(&req);
        }

        this->denied_blocked = false;
        this->denied_writes.clear();
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
    vp::IoReq *req = this->free_bursts.front();
    this->free_bursts.pop();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Enqueueing %s burst (burst: %p, base: 0x%lx, size: 0x%lx)\n",
        is_write ? "write" : "read", req, base, size);

    req->prepare();
    req->set_is_write(is_write);
    req->set_addr(base);
    req->set_size(size);
    req->is_first = true;
    req->is_last = true;
    req->burst_id = -1;
    req->set_resp_status(vp::IO_RESP_OK);

    BurstInfo *info = (BurstInfo *)req->initiator;
    info->transfer = transfer;
    info->ready_cycle = 0;

    this->pending_bursts.push(req);
    this->pending_bursts_ack.push(req);

    if (this->pending_bursts.size() == 1)
    {
        this->current_burst_base = this->pending_bursts.front()->get_addr();
        this->current_burst_size = this->pending_bursts.front()->get_size();
    }

    this->update();
}



void IDmaBeAxi::read_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, false, transfer);
}



void IDmaBeAxi::send_read_burst_to_axi()
{
    // Peek the head; do not pop until the router has accepted the request.
    vp::IoReq *req = this->pending_bursts.front();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Sending read burst to AXI (burst: %p, base: 0x%lx, size: 0x%lx)\n",
        req, req->get_addr(), req->get_size());

    req->prepare();

    vp::IoReqStatus status = this->ico_itf.req(req);

    if (status == vp::IO_REQ_DENIED)
    {
        // Router cannot accept this request right now. Leave it at the head
        // of pending_bursts and stop issuing until axi_retry() fires.
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Read burst denied by AXI (burst: %p)\n", req);
        this->denied_blocked = true;
        return;
    }

    // Accepted — pop now, then propagate back to producers.
    this->pending_bursts.pop();
    this->update();
    this->be->update();

    if (status == vp::IO_REQ_DONE)
    {
        if (req->get_resp_status() == vp::IO_RESP_INVALID)
        {
            trace.force_warning("Invalid access during AXI read burst (base: 0x%lx, size: 0x%lx)\n",
                req->get_addr(), req->get_size());
        }
        // Treat sync DONE the same way as v1 IO_REQ_OK: queue the burst with
        // its annotated latency so it is delivered downstream after the right
        // number of cycles.
        this->read_handle_req_end(req);
    }
    // IO_REQ_GRANTED: response will arrive later via axi_response().
}



void IDmaBeAxi::read_handle_req_end(vp::IoReq *req)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Handling end of read request (req: %p, latency %d)\n",
        req, req->get_latency());

    BurstInfo *info = (BurstInfo *)req->initiator;
    info->ready_cycle = this->clock.get_cycles() + req->get_latency();

    this->read_waiting_bursts.push(req);
    this->fsm_event.enqueue(std::max(req->get_latency(), (int64_t)1));
}



void IDmaBeAxi::write_data_ack(uint8_t *data)
{
    vp::IoReq *req = this->read_bursts_waiting_ack.front();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Acknowleged burst (burst: %p)\n", req);

    this->read_bursts_waiting_ack.pop();
    this->free_bursts.push(req);

    this->be->update();
    this->fsm_event.enqueue();
}



void IDmaBeAxi::axi_response(vp::Block *__this, vp::IoReq *req)
{
    IDmaBeAxi *_this = (IDmaBeAxi *)__this;

    if (req->get_is_write())
    {
        _this->write_handle_req_end(req);
    }
    else
    {
        _this->read_handle_req_end(req);
    }
}



void IDmaBeAxi::axi_retry(vp::Block *__this)
{
    IDmaBeAxi *_this = (IDmaBeAxi *)__this;

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "AXI retry — resuming issue\n");
    _this->denied_blocked = false;
    _this->update();
}



void IDmaBeAxi::write_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, true, transfer);
}



void IDmaBeAxi::write_data(IdmaTransfer *transfer, uint8_t *data, uint64_t size)
{
    vp::IoReq *req = new vp::IoReq();

    uint64_t base = this->current_burst_base;
    this->current_burst_base += size;

    this->current_burst_size -= size;
    if (this->current_burst_size == 0)
    {
        this->pending_bursts.pop();

        if (this->pending_bursts.size() > 0)
        {
            this->current_burst_base = this->pending_bursts.front()->get_addr();
            this->current_burst_size = this->pending_bursts.front()->get_size();
        }
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Write data (req: %p, base: 0x%lx, size: 0x%lx)\n",
        req, base, size);

    req->prepare();
    req->set_is_write(true);
    req->set_addr(base);
    req->set_size(size);
    req->set_data(data);
    req->is_first = true;
    req->is_last = true;
    req->burst_id = -1;
    req->set_resp_status(vp::IO_RESP_OK);
    // Single-shot writes carry the transfer pointer directly in `initiator`.
    req->initiator = (void *)transfer;

    if (this->denied_blocked)
    {
        // Router has already denied us. Queue this write behind the others
        // and let axi_retry() drain them.
        this->denied_writes.push_back(req);
        return;
    }

    vp::IoReqStatus status = this->ico_itf.req(req);

    if (status == vp::IO_REQ_DENIED)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Write data denied by AXI (req: %p)\n", req);
        this->denied_blocked = true;
        this->denied_writes.push_back(req);
        return;
    }

    if (status == vp::IO_REQ_DONE)
    {
        if (req->get_resp_status() == vp::IO_RESP_INVALID)
        {
            trace.force_warning("Invalid access during AXI write burst (base: 0x%lx, size: 0x%lx)\n",
                base, size);
        }
        this->write_handle_req_end(req);
    }
    // IO_REQ_GRANTED: response handled in axi_response().
}



void IDmaBeAxi::write_handle_req_end(vp::IoReq *req)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Handling end of request (req: %p)\n", req);

    IdmaTransfer *transfer = (IdmaTransfer *)req->initiator;
    this->be->ack_data(transfer, req->get_data(), req->get_size());

    vp::IoReq *burst = this->pending_bursts_ack.front();
    burst->set_size(burst->get_size() - req->get_size());

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Updating burst remaining size (burst: %p, req_size: %d, burst_size: %d)\n",
        burst, req->get_size(), burst->get_size());

    if (burst->get_size() == 0)
    {
        this->pending_bursts_ack.pop();
        this->free_bursts.push(burst);
        this->be->update();
        this->update();
    }

    delete req;

    // Write latency is intentionally ignored — preserved from v1.
}



bool IDmaBeAxi::can_accept_burst()
{
    return this->free_bursts.size();
}

bool IDmaBeAxi::can_accept_data()
{
    return true;
}



void IDmaBeAxi::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaBeAxi *_this = (IDmaBeAxi *)__this;

    // 1. Drain any write requests stashed during a deny.
    while (!_this->denied_blocked && !_this->denied_writes.empty())
    {
        vp::IoReq *req = _this->denied_writes.front();
        vp::IoReqStatus status = _this->ico_itf.req(req);

        if (status == vp::IO_REQ_DENIED)
        {
            _this->denied_blocked = true;
            return;
        }

        _this->denied_writes.pop_front();

        if (status == vp::IO_REQ_DONE)
        {
            if (req->get_resp_status() == vp::IO_RESP_INVALID)
            {
                _this->trace.force_warning("Invalid access during AXI write retry (base: 0x%lx, size: 0x%lx)\n",
                    req->get_addr(), req->get_size());
            }
            _this->write_handle_req_end(req);
        }
        // IO_REQ_GRANTED: handled in axi_response().
    }

    // 2. Issue the next pending read burst, if any.
    if (!_this->denied_blocked
        && _this->pending_bursts.size() > 0
        && !_this->pending_bursts.front()->get_is_write())
    {
        _this->send_read_burst_to_axi();
    }

    // 3. Forward read data to the destination backend once the read latency
    //    has elapsed and the destination is ready.
    if (_this->read_waiting_bursts.size() != 0)
    {
        vp::IoReq *req = _this->read_waiting_bursts.front();
        BurstInfo *info = (BurstInfo *)req->initiator;

        if (_this->be->is_ready_to_accept_data(info->transfer))
        {
            if (info->ready_cycle <= _this->clock.get_cycles())
            {
                _this->read_waiting_bursts.pop();
                _this->read_bursts_waiting_ack.push(req);

                _this->be->write_data(info->transfer, req->get_data(), req->get_size());

                _this->fsm_event.enqueue();
            }
            else
            {
                _this->fsm_event.enqueue(info->ready_cycle - _this->clock.get_cycles());
            }
        }
    }
}



void IDmaBeAxi::update()
{
    this->fsm_event.enqueue();
}


bool IDmaBeAxi::is_empty()
{
    return this->pending_bursts.empty();
}
