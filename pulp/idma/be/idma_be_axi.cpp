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
    // Backend will be used later for interaction
    this->be = be;

    // Declare master port to AXI interface
    this->ico_itf.set_resp_meth(&IDmaBeAxi::axi_response);
    idma->new_master_port(itf_name, &this->ico_itf, this);

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Get the top parameter giving the maximum number of outstanding bursts to AXI interconnect
    int burst_queue_size = idma->get_js_config()->get_int("burst_queue_size");

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
    // Each chunk is directly sent to AXI to avoid sending whole burst at the end.
    // Allocate a request and send it. The burst limitation is modeled with another request
    vp::IoReq *req = new vp::IoReq();

    uint64_t base = this->current_burst_base;
    this->current_burst_base += size;

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Write data (req: %p, base: 0x%lx, size: 0x%lx)\n",
        req, base, size);

    req->prepare();
    req->set_is_write(true);
    req->set_addr(base);
    req->set_size(size);
    req->set_data(data);
    *req->arg_get(0) = (void *)transfer;

    vp::IoReqStatus status = this->ico_itf.req(req);
    if (status == vp::IoReqStatus::IO_REQ_OK)
    {
        this->write_handle_req_end(req);
    }
    else if (status == vp::IoReqStatus::IO_REQ_INVALID)
    {
        trace.force_warning("Invalid access during AXI write burst (base: 0x%lx, size: 0x%lx)\n",
            base, size);
    }
    else
    {
        // In case of asynchronous response, we do nothing, this will be handled when the response
        // is received through the callback
    }
}



void IDmaBeAxi::write_handle_req_end(vp::IoReq *req)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Handling end of request (req: %p)\n", req);

    // Acknowledge now the data since they are gone, to let the other backend protocol sending the
    // rest of the burst immediately
    vp::IoReq *burst = this->pending_bursts.front(); //GET THE BURST VARIABLE
    //this->be->ack_data((IdmaTransfer *)*req->arg_get(0), req->get_data(), req->get_size());

    //printf("IDMA TRANSFER ADDR IS %p REQ ADDR IS %p\n",(void *)*burst->arg_get(0),(IdmaTransfer *)*req->arg_get(0)); 
    this->be->ack_data((IdmaTransfer *)*burst->arg_get(0), req->get_data(), req->get_size()); //ACK DATA WITH BURST VARIABLE

    // Account this chunk on the first pending burst
    //vp::IoReq *burst = this->pending_bursts.front();
    burst->set_size(burst->get_size() - req->get_size());

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Updating burst remaining size (burst: %p, req_size: %d, burst_size: %d)\n",
        burst, req->get_size(), burst->get_size());

    // And release it in case it is done
    if (burst->get_size() == 0)
    {
        this->pending_bursts.pop();
        if (this->pending_bursts.size() > 0)
        {
            this->current_burst_base = this->pending_bursts.front()->get_addr();
        }
        this->free_bursts.push(burst);
        // Notify the backend since it may schedule another burst
        this->be->update();
        // Update FSM since we updated current burst, we have have something to do
        this->update();
    }

    delete req;

    // For now we ignore the latency for write requests.
    // This will be better modeled when we switch to the new AXI router
}



bool IDmaBeAxi::can_accept_burst()
{
    // We can accept a burst as soon as one is available
    return this->free_bursts.size();
}

bool IDmaBeAxi::can_accept_data()
{
    // Data is always accepted since it is directly sent to interconnect
    return true;
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
