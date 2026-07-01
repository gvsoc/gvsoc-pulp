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
    // Backend will be used later for interaction
    this->be = be;

    // Declare master port to AXI interface
    this->ico_itf.set_resp_meth(&IDmaBeAxi::axi_response);
    // Also register the grant method so we are notified when a denied write is accepted
    // by the NoC NI, which is how we honour write back-pressure.
    this->ico_itf.set_grant_meth(&IDmaBeAxi::axi_grant);
    idma->new_master_port(itf_name, &this->ico_itf, this);

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Get the top parameter giving the maximum number of outstanding bursts to AXI interconnect
    int burst_queue_size = idma->get_js_config()->get_int("burst_queue_size");

    // Get the top parameter giving the maximum burst size for AXI transactions. TCDM will alight to this parameter
    this->burst_size = idma->get_js_config()->get_int("burst_size");

    // Use it to size the array of bursts and associated timestamps
    this->bursts.resize(burst_queue_size);
    this->read_timestamps.resize(burst_queue_size);

    for (int i=0; i<burst_queue_size; i++)
    {
        // This ID is used to index the array of timestamps to schedule when a burst
        // is done.
        this->bursts[i].id = i;

        // Since bursts are allocated statically, also allocate the data to handle maximum
        // burst size, this will avoid allcoating and freeing it during execution.
        // Note that bursts are only used for reading. Writing is using dynamically allocated
        // requests to fit the other backend data chunks.
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
        // Since requests are here and there in various queues, we need to first
        // clear all the queues
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

        // And put back them all as free
        for (vp::IoReq &req: this->bursts)
        {
            // Reserve one argument in each request, we'll use it to store the associated
            // transfer to properly report the request termination
            req.arg_alloc();
            this->free_bursts.push(&req);
        }

        // Note that to be safe,
        // if any request is pending outside this component, the convention is that
        // any component inside the same reset domain will just release the request, while
        // components outside will block the reset until the requests are back.
    }
}



uint64_t IDmaBeAxi::get_burst_size(uint64_t base, uint64_t size)
{
    // First check maximum burst size
    size = std::min(size, (uint64_t)AXI_PAGE_SIZE);

    if (this->burst_size>0) {
        //Constraint AXI burst size
        size = std::min(size, (uint64_t)this->burst_size);
    }

    // Then page-crossing
    uint64_t next_page = (base + AXI_PAGE_SIZE - 1) & ~(AXI_PAGE_SIZE - 1);
    if (next_page > base)
    {
        size = std::min(next_page - base, size);
    }

    return size;
}



void IDmaBeAxi::enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer)
{
    // Get a free burst, this method is called only if at least one is free
    vp::IoReq *req = this->free_bursts.front();
    this->free_bursts.pop();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Enqueueing %s burst (burst: %p, base: 0x%lx, size: 0x%lx)\n",
        is_write ? "write" : "read", req, base, size);

    // Enqueue the burst to the pending queue
    req->prepare();
    req->set_is_write(is_write);
    req->set_addr(base);
    req->set_size(size);
    *req->arg_get(0) = (void *)transfer;

    this->pending_bursts.push(req);

    // It case it is the first burst, set the pending base, this is used for writing bursts to know next
    // req address
    if (this->pending_bursts.size() == 1)
    {
        this->current_burst_base = this->pending_bursts.front()->get_addr();
        this->current_burst_size = this->pending_bursts.front()->get_size();
    }

    // And trigger the FSM in case it needs to be processed now
    this->update();
}



void IDmaBeAxi::read_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, false, transfer);
}



void IDmaBeAxi::send_read_burst_to_axi()
{
    // Dequeue the burst from pending queue
    vp::IoReq *req = this->pending_bursts.front();
    this->pending_bursts.pop();
    // Trigger the FSM in case another burst must be processed
    this->update();
    // And also trigger the middle-end in case it has another burst to push
    this->be->update();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Sending read burst to AXI (burst: %p, base: 0x%lx, size: 0x%lx, latency:%d)\n",
        req, req->get_addr(), req->get_size(), req->get_latency());

    // Reinit timings
    req->prepare();

    // Send to AXI interface
    vp::IoReqStatus status = this->ico_itf.req(req);

    if (status == vp::IoReqStatus::IO_REQ_OK)
    {
        // To simplify, we always queue the response with associated latency
        // and handle it from the FSM, once the latency is reached
        this->read_handle_req_end(req);
    }
    else if (status == vp::IoReqStatus::IO_REQ_INVALID)
    {
        trace.force_warning("Invalid access during AXI read burst (base: 0x%lx, size: 0x%lx)\n",
            req->get_addr(), req->get_size());
    }
    else
    {
        // In case of asynchronous response, we do nothing, this will be handled when the response
        // is received through the callback
    }
}



void IDmaBeAxi::read_handle_req_end(vp::IoReq *req)
{
    // Remember at which timestamp the burst must be notified
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Handling end of read request (req: %p, latency %d)\n", req, req->get_latency());
    this->read_timestamps[req->id] = this->clock.get_cycles() + req->get_latency();
    // Queue the requests, they will be notified in order.
    this->read_waiting_bursts.push(req);
    // Enqueue fsm event at desired timestamp in case the event is not already enqueued before
    this->fsm_event.enqueue(std::max(req->get_latency(), (uint64_t)1));
}



void IDmaBeAxi::write_data_ack(uint8_t *data)
{
    // We got an acknowledge for a burst pushed to target, move it to the free queue
    vp::IoReq *req = this->read_bursts_waiting_ack.front();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Acknowleged burst (burst: %p)\n", req);

    this->read_bursts_waiting_ack.pop();
    this->free_bursts.push(req);

    // Since a new request is now free, notify the backend in case it was waiting for it
    this->be->update();
    this->fsm_event.enqueue();
}



void IDmaBeAxi::axi_response(vp::Block *__this, vp::IoReq *req)
{
    IDmaBeAxi *_this = (IDmaBeAxi *)__this;

    // Just enqueue the response, it will be processed at the right timestamp, depending
    // on latency
    if (req->get_is_write())
    {
        _this->write_handle_req_end(req);
    }
    else
    {
        _this->read_handle_req_end(req);
    }
}



void IDmaBeAxi::write_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, true, transfer);
}



void IDmaBeAxi::write_data(IdmaTransfer *transfer, uint8_t *data, uint64_t size)
{
    // Coalesce the per-line beats coming from the source backend into a single AXI burst,
    // and inject that burst into the NoC AS SOON AS its first beat arrives, streaming the
    // remaining beats into its buffer while the NoC is already forwarding it.
    //
    // Why inject on the first beat instead of waiting for the whole burst:
    // The transfer is now considered complete only when the NoC reports the write as done
    // (the burst response, see write_handle_req_end), not when the source has finished
    // READING. This is required so that the per-beat NoC congestion (which is what makes a
    // far tile slower than a near one) is actually accounted for, even for a single-burst
    // transfer: with the previous early-ack-on-read scheme the counter stopped at read time,
    // which is not congested (each tile reads its own local L1), so small transfers showed no
    // distance scaling at all.
    // But the read and the write physically overlap in hardware (a beat is read from L1 and
    // put on the W channel in the same cycle). If we waited for the full burst before sending
    // it, we would serialise read-then-stream and count the streaming twice. Injecting on the
    // first beat overlaps the two: the NoC streams beat i while the source is still filling
    // beat i+1. The source fills the buffer at one line/cycle starting before the NoC reads
    // each beat at the destination (after the router traversal latency), so every beat is in
    // place by the time it is actually written.
    vp::IoReq *burst = this->pending_bursts.front();
    uint64_t offset = this->current_burst_base - burst->get_addr();
    bool first_beat = (offset == 0);

    memcpy(burst->get_data() + offset, data, size);

    this->current_burst_base += size;
    this->current_burst_size -= size;
    bool burst_done = (this->current_burst_size == 0);

    // Release the source line buffer right away (flow control). The data is already copied
    // into the burst buffer, so the source can keep streaming. This does NOT account the data
    // as written: completion is reported later, when the NoC write response arrives.
    this->be->ack_data_buffer(transfer, data);

    if (first_beat)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Sending write burst to AXI (burst: %p, base: 0x%lx, size: 0x%lx)\n",
            burst, burst->get_addr(), burst->get_size());

        // Send the whole burst as one request. Its addr/size were set in enqueue_burst and its
        // data buffer is being filled in place (the NoC only references it, it does not copy it
        // synchronously, so injecting before the buffer is fully filled is safe).
        burst->prepare();
        burst->set_is_write(true);

        vp::IoReqStatus status = this->ico_itf.req(burst);
        if (status == vp::IoReqStatus::IO_REQ_INVALID)
        {
            trace.force_warning("Invalid access during AXI write burst (base: 0x%lx, size: 0x%lx)\n",
                burst->get_addr(), burst->get_size());
        }
        else if (status == vp::IoReqStatus::IO_REQ_OK)
        {
            // Synchronous completion (no NoC latency): account and release the slot now.
            this->write_handle_req_end(burst);
        }
        // IO_REQ_PENDING / IO_REQ_DENIED: the NoC keeps the burst and will respond later (a
        // denied burst is queued in the NI and granted when it can be streamed). The slot and
        // the write accounting are handled on that response. Back-pressure is naturally
        // applied by the outstanding-burst slots (can_accept_burst): a slot is held until the
        // real completion, so when the NoC is congested slots free later and the source stalls.
    }

    if (burst_done)
    {
        // The burst is fully assembled. Stop tracking it as the one being filled and advance
        // to the next pending burst (its base/size tell where the next beats go). The burst
        // itself stays in flight until its NoC response frees the slot.
        this->pending_bursts.pop();
        if (this->pending_bursts.size() > 0)
        {
            this->current_burst_base = this->pending_bursts.front()->get_addr();
            this->current_burst_size = this->pending_bursts.front()->get_size();
        }
    }
}



void IDmaBeAxi::write_handle_req_end(vp::IoReq *req)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Handling end of write burst (req: %p)\n", req);

    // The write burst has really been committed to the destination by the NoC. This response
    // time carries the per-beat congestion the burst went through, so this is the point where
    // we account the burst as written and possibly terminate the transfer. Reporting completion
    // here (and not at source-read time) is what makes the timing scale with tile distance.
    IdmaTransfer *transfer = (IdmaTransfer *)*req->arg_get(0);
    this->be->ack_write_completed(transfer, req->get_size());

    // Release the burst slot so the middle-end can push another burst. Holding the slot until
    // the real completion is also what back-pressures the source when the NoC is congested:
    // responses come back later, slots stay busy longer, so new bursts (and the reads feeding
    // them) are throttled.
    this->free_bursts.push(req);
    this->be->update();
    this->update();
}



bool IDmaBeAxi::can_accept_burst()
{
    // We can accept a burst as soon as one is available
    return this->free_bursts.size();
}

bool IDmaBeAxi::can_accept_data()
{
    // Data for the current burst is always accepted: the burst buffer can hold the whole
    // burst and the back-pressure is applied at burst granularity through the outstanding
    // slots (can_accept_burst), not by refusing beats. So a beat is never stalled here.
    return true;
}



void IDmaBeAxi::axi_grant(vp::Block *__this, vp::IoReq *req)
{
    IDmaBeAxi *_this = (IDmaBeAxi *)__this;

    // A previously denied write burst has been granted by the NoC NI: it is now being
    // streamed and will produce a response when committed. There is nothing to do here
    // anymore (no deferred ack): completion and slot release happen on that response. The
    // method is kept because the grant port is registered on the AXI master interface.
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Write burst granted by NoC (req: %p)\n", req);
}



void IDmaBeAxi::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaBeAxi *_this = (IDmaBeAxi *)__this;

    // At each cycle, ,if the next burst is a read one, we send it
    if (_this->pending_bursts.size() > 0 && !_this->pending_bursts.front()->get_is_write())
    {
        _this->send_read_burst_to_axi();
    }

    // In case we have pending read bursts waiting for pushing data, only do it if the backend
    // is ready to accept the data in case the destination is not ready
    if (_this->read_waiting_bursts.size() != 0 &&
        _this->be->is_ready_to_accept_data((IdmaTransfer *)*_this->read_waiting_bursts.front()->arg_get(0)))
    {
        vp::IoReq *req = _this->read_waiting_bursts.front();

        // Push the data only once the timestamp has expired to take into account the latency
        // returned when the data was read
        if (_this->read_timestamps[req->id] <= _this->clock.get_cycles())
        {
            // Move the burst to a different queue so that we can free the request when it is
            // acknowledge
            _this->read_waiting_bursts.pop();
            _this->read_bursts_waiting_ack.push(req);

            // Send the data
            _this->be->write_data((IdmaTransfer *)*req->arg_get(0), req->get_data(), req->get_size());

            // Trigger again the FSM since we may continue with another transfer
            _this->fsm_event.enqueue();
        }
        else
        {
            // Otherwise check again when timetamp is reached
            _this->fsm_event.enqueue(_this->read_timestamps[req->id] - _this->clock.get_cycles());
        }
    }
}



void IDmaBeAxi::update()
{
    // Trigger the event to check if any action should be taken
    this->fsm_event.enqueue();
}


bool IDmaBeAxi::is_empty()
{
    return this->pending_bursts.empty();
}
