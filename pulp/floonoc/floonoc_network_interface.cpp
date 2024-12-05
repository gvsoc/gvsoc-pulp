/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "floonoc.hpp"
#include "floonoc_router.hpp"
#include "floonoc_network_interface.hpp"



NetworkInterface::NetworkInterface(FlooNoc *noc, int x, int y)
    : vp::Block(noc, "ni_" + std::to_string(x) + "_" + std::to_string(y)),
    fsm_event(this, &NetworkInterface::fsm_handler),
    response_event(this, &NetworkInterface::response_handler)
{
    this->noc = noc;
    this->x = x;
    this->y = y;
    this->max_input_req = 4;

    traces.new_trace("trace", &trace, vp::DEBUG);

    // Network interface input port
    this->narrow_input_itf.set_req_meth(&NetworkInterface::narrow_req);
    noc->new_slave_port("narrow_input_" + std::to_string(x) + "_"  + std::to_string(y),
        &this->narrow_input_itf, this);
    this->wide_input_itf.set_req_meth(&NetworkInterface::wide_req);
    noc->new_slave_port("wide_input_" + std::to_string(x) + "_"  + std::to_string(y),
        &this->wide_input_itf, this);


    // Create one req for each possible outstanding req.
    // Internal requests will be taken from here to model the fact only a limited number
    // of requests can be sent at the same time
    int ni_outstanding_reqs = this->noc->get_js_config()->get("ni_outstanding_reqs")->get_int();
    for (int i=0; i<ni_outstanding_reqs; i++)
    {
        this->free_reqs.push(new vp::IoReq());
    }
}



void NetworkInterface::reset(bool active)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Resetting network interface\n");
    if (active)
    {
        this->stalled = false;
        this->pending_burst_size = 0;
        this->nb_pending_input_req = 0;
        this->denied_req = NULL;
        this->pending_burst_waiting_for_req = false;
    }
}



void NetworkInterface::unstall_queue(int from_x, int from_y)
{
    // The request which was previously denied has been granted. Unstall the output queue
    // and schedule the FSM handler to check if something has to be done
    this->stalled = false;
    this->fsm_event.enqueue();
}



void NetworkInterface::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    NetworkInterface *_this = (NetworkInterface *)__this;

    // We get there when we may have a request to send. This can happen if:
    // - The network interface is not stalled due to a denied request
    // - There is at least one burst pending
    // - The timestamp of the first pending burst has passed
    // - There is at least one available internal request
    if (!_this->stalled
        && _this->pending_bursts.size() > 0
        && _this->pending_bursts_timestamp.front() <= _this->clock.get_cycles()
        && _this->free_reqs.size() > 0
        && !_this->pending_burst_waiting_for_req)
    {
        vp::IoReq *burst = _this->pending_bursts.front();
        bool first_time = false;
        bool wide = *(bool *)burst->arg_get(FlooNoc::REQ_WIDE);
        // In case the burst is being handled for the first time, initialize the current burst
        if (_this->pending_burst_size == 0)
        {
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Start handling burst (burst: %p, base: 0x%x, size: 0x%x, is_write: %d)\n",
                burst, burst->get_addr(), burst->get_size(), burst->get_is_write());

            // By default, we consider the whole burst as valid. In one of the burst request is\
            // detected invalid, we will mark the whole burst as invalid
            burst->status = vp::IO_REQ_OK;
            _this->pending_burst_base = burst->get_addr();
            _this->pending_burst_data = burst->get_data();
            _this->pending_burst_size = burst->get_size();
            // We use one data in the current burst to store the remaining size and know when the
            // last internal request has been handled to notify the end of burst
            *(int *)burst->arg_get_last() = burst->get_size();

            // First send a dummy request to simulate the "forward" request of the noc
            if (wide){ // For now only on wide noc. because narrow is loaded already due to missing I Cache
                first_time = true;
            }
            else{
                first_time = false;
            }

        }

        uint64_t width = wide ? _this->noc->wide_width : _this->noc->narrow_width;
        // Then pop an internal request and fill it from current burst information
        vp::IoReq *req = _this->free_reqs.front();
        _this->free_reqs.pop();

        // Get base from current burst
        uint64_t base = _this->pending_burst_base;

        // Size must be at max the noc width to respect the bandwidth
        uint64_t size = std::min(width, _this->pending_burst_size);
        // And must not cross a page to fall into one target
        uint64_t next_page = (base + width - 1) & ~(width - 1);
        if (next_page > base)
        {
            size = std::min(next_page - base, size);
        }

        // Fill-in information. Request data is used to store temporary information that we will
        // need later
        req->init();
        req->arg_alloc(FlooNoc::REQ_NB_ARGS);
        *req->arg_get(FlooNoc::REQ_DEST_NI) = (void *)_this;
        *req->arg_get(FlooNoc::REQ_DEST_BURST) = (void *)burst;
        *req->arg_get(FlooNoc::REQ_DEST_BASE) = (void *)base;
        *req->arg_get(FlooNoc::REQ_WIDE) = (void *)wide;
        *req->arg_get(FlooNoc::REQ_IS_ADDRESS) = first_time ? (void *) 1: (void *) 0;
        req->set_size(size);
        req->set_data(_this->pending_burst_data);
        req->set_is_write(burst->get_is_write());
        req->set_opcode(burst->get_opcode());
        req->set_second_data(burst->get_second_data());
        // Get the target entry corresponding to the current base
        Entry *entry = _this->noc->get_entry(base, size);
        if (entry == NULL)
        {
            // If any request of the burst is invalid because no target was found, make the whole
            // burst invalid.
            _this->trace.force_warning("No entry found for burst (base: 0x%x, size: 0x%x)",
                base, size);
            burst->status = vp::IO_REQ_INVALID;

            // Stop the burst
            *(int *)burst->arg_get_last() -= _this->pending_burst_size;
            _this->pending_burst_size = 0;
            _this->pending_bursts.pop();
            _this->pending_bursts_timestamp.pop();
            // And respond to the current burst as it is over with an error only if there is
            // no request on-going for it.
            // Otherwise, the response will be sent when last request response is received
            if (*(int *)burst->arg_get_last() == 0)
            {
                burst->get_resp_port()->resp(burst);
            }
        }
        else if (*req->arg_get(FlooNoc::REQ_IS_ADDRESS)){
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Sending dummy req request to noc (req: %p, base: 0x%x, size: 0x%x, destination: (%d, %d))\n",
                req, req->get_addr(), size, entry->x, entry->y);
            _this->pending_burst_waiting_for_req = true;
            first_time = false;
            req->set_addr(base - entry->remove_offset);
            *req->arg_get(FlooNoc::REQ_DEST_X) = (void *)(long)entry->x;
            *req->arg_get(FlooNoc::REQ_DEST_Y) = (void *)(long)entry->y;

            // Note that the router may not grant the request if its input queue is full.
            // In this case we must stall the network interface
            Router *router = _this->noc->get_router(_this->x, _this->y, wide, req->get_is_write(), true);
            _this->stalled = router->handle_request(req, _this->x, _this->y);
        }
        else 
        {
            // Update the current burst for next request
            _this->pending_burst_base += size;
            _this->pending_burst_data += size;
            _this->pending_burst_size -= size;

            // And remove the burst if all requests were sent. Note that this will allow next burst
            // to be processed even though some requests may still be on-going for it.
            if (_this->pending_burst_size == 0)
            {
                _this->pending_bursts.pop();
                _this->pending_bursts_timestamp.pop();
                _this->nb_pending_input_req--;
            }

            // Store information in the request which will be needed by the routers and the target
            req->set_addr(base - entry->remove_offset);
            *req->arg_get(FlooNoc::REQ_DEST_X) = (void *)(long)entry->x;
            *req->arg_get(FlooNoc::REQ_DEST_Y) = (void *)(long)entry->y;
            // And forward to the first router which is at the same position as the network
            // interface
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Injecting request to noc (req: %p, base: 0x%x, size: 0x%x, destination: (%d, %d))\n",
                req, req->get_addr(), size, entry->x, entry->y);

            // Note that the router may not grant the request if its input queue is full.
            // In this case we must stall the network interface

            Router *router = _this->noc->get_router(_this->x, _this->y, wide, req->get_is_write(), false);
            _this->stalled = router->handle_request(req, _this->x, _this->y);
        }

        // Now that we removed a pending req, we may need to unstall a denied request
        if (_this->denied_req && _this->nb_pending_input_req != _this->max_input_req)
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling denied request (req: %p)\n", _this->denied_req);
            vp::IoReq *req = _this->denied_req;
            _this->denied_req = NULL;
            req->get_resp_port()->grant(req);
        }

        // Since we processed a burst, we need to check again in the next cycle if there is
        // anything to do.
        _this->fsm_event.enqueue();
    }
    else if (!_this->stalled && _this->pending_bursts.size() > 0 && _this->free_reqs.size() > 0 && !_this->pending_burst_waiting_for_req)
    {
        // If we did not handle the first pending burst because we haven't reached its
        // timestamp, schedule the event at this timestamp to be able to process it
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Nothing todo. Enqueuing response handler for timestamp: %ld)\n", _this->pending_bursts_timestamp.front());
        _this->fsm_event.enqueue(
            _this->pending_bursts_timestamp.front() - _this->clock.get_cycles()
        );
    }
}


void NetworkInterface::response_handler(vp::Block *__this, vp::ClockEvent *event)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->trace.msg(vp::DEBUG, "Delayed reponse handler\n");

    
    int qsize = _this->pending_send_target_reqs.size();
    uint64_t next_timestamp = UINT64_MAX;
    for (int i = 0; i < qsize; i++)
    {   
        vp::IoReq *req =  _this->pending_send_target_reqs.front();
        uint64_t timestamp = _this->pending_send_target_timestamps.front();
        if (timestamp <= _this->clock.get_cycles())
        {
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handling response (req: %p)\n", req);
            _this->pending_send_target_reqs.pop();
            _this->pending_send_target_timestamps.pop();
            _this->noc->handle_request_end(req);
        }
        else{
            next_timestamp = min(next_timestamp, timestamp);
        }
    }
    if (next_timestamp != UINT64_MAX)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Too early, Enqueuing response handler for timestamp: %ld)\n", req, next_timestamp);
        _this->response_event.enqueue(next_timestamp - _this->clock.get_cycles()); // Maybe need to search for next timestamp and only enque once
    }
}


void NetworkInterface::handle_response(vp::IoReq *req)
{
    // This gets called by the routers when an internal request has been handled
    // First extract the corresponding burst from the request so that we can update the burst.
    vp::IoReq *burst = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_DEST_BURST);
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request response (req: %p)\n", req);

    // If at least one of the request is invalid, this makes the whole burst invalid
    if (req->status == vp::IO_REQ_INVALID)
    {
        burst->status = vp::IO_REQ_INVALID;
    }

    if(*req->arg_get(FlooNoc::REQ_IS_ADDRESS)){
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished dummy req request to noc (req: %p)\n", req);
        this->pending_burst_waiting_for_req = false;
    }
    else{
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Reducing remaining size of burst (burst: %p, size: %d, req: %p, size %d)\n",
        burst, *(int *)burst->arg_get_last(), req, req->get_size());
        // Account the received response on the burst
        *(int *)burst->arg_get_last() -= req->get_size();
    }
    // And respond to it if all responses have been received
    if (*(int *)burst->arg_get_last() == 0)
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished %s burst (burst: %p, latency: %d)\n", burst->get_int(FlooNoc::REQ_WIDE)?"wide":"narrow",burst, burst->get_latency());
        burst->get_resp_port()->resp(burst);
    }

    // The request is now available
    this->free_reqs.push(req);
    // Trigger the FSM since something may need to be done now that a new request is available
    this->fsm_event.enqueue();
}



vp::IoReqStatus NetworkInterface::narrow_req(vp::Block *__this, vp::IoReq *req)
{
    // This gets called when a burst is received
    NetworkInterface *_this = (NetworkInterface *)__this;
    *req->arg_get(FlooNoc::REQ_WIDE) = (void *) 0;
    vp::IoReqStatus result = _this->req(_this, req);
    return result;
}

vp::IoReqStatus NetworkInterface::wide_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    *req->arg_get(FlooNoc::REQ_WIDE) = (void *) 1;
    vp::IoReqStatus result = _this->req(_this, req);
    return result;
}

vp::IoReqStatus NetworkInterface::req(vp::Block *__this, vp::IoReq *req)
{
    // This gets called when a burst is received
    NetworkInterface *_this = (NetworkInterface *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received %s burst (burst: %p, offset: 0x%x, size: 0x%x, is_write: %d, op: %d)\n",
        req->get_int(FlooNoc::REQ_WIDE)?"wide":"narrow",req, offset, size, req->get_is_write(), req->get_opcode());

    int dest_x = req->get_int(FlooNoc::REQ_DEST_X);
    int dest_y = req->get_int(FlooNoc::REQ_DEST_Y);
    // *req->arg_get(FlooNoc::REQ_IS_ADDRESS) = (void*) 1;

    // Just enqueue it and trigger the FSM which will check if it must be processed now
    _this->pending_bursts.push(req);
    // We also need to push at which timestamp the burst can start being processed.
    // Since we handle it asynchronously, we need to start it only once its latency has been
    // reached
    _this->pending_bursts_timestamp.push(_this->clock.get_cycles() + req->get_latency());
    // We must also reset it, since it will be covered by the asynchronous reply
    req->set_latency(0);
    _this->fsm_event.enqueue(
        std::max((int64_t)0, _this->pending_bursts_timestamp.front() - _this->clock.get_cycles())
    );

    _this->nb_pending_input_req++;

    // Only accept the request if we don't have too many pending requests
    if (_this->nb_pending_input_req == _this->max_input_req)
    {
        _this->denied_req = req;
        return vp::IO_REQ_DENIED;
    }
    else
    {
        return vp::IO_REQ_PENDING;
    }
}


vp::IoReqStatus NetworkInterface::send_to_target(vp::IoReq *req, int pos_x, int pos_y)
{
    vp::IoMaster *target = this->noc->get_target(pos_x, pos_y);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Sending request to target (target name: %s)(req: %p, base: 0x%x, size: 0x%x, position: (%d, %d))\n",
        target->get_name().c_str(), req, req->get_addr(), req->get_size() ,pos_x, pos_y);
    vp::IoReqStatus result = target->req(req);
    if (result == vp::IO_REQ_OK || result == vp::IO_REQ_INVALID)
    {
        // If the request is processed synchronously, immediately notify the network interface
        req->status = result;
        if(req->get_latency() > 1 && (req->get_opcode() == vp::IoReqOpcode::READ || req->get_opcode() == vp::IoReqOpcode::WRITE))
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Delaying response because request has a latency of %d\n", req->get_latency());
            // The target might have a latency, but still return sychronosuly. In this case, we need to account for the latency
            this->pending_send_target_reqs.push(req);
            this->pending_send_target_timestamps.push(this->clock.get_cycles() + req->get_latency());
            this->response_event.enqueue(req->get_latency());
            return vp::IO_REQ_PENDING;
        }
        else{

            this->noc->handle_request_end(req);
            return result;
        }

        // We need to store the status in the request so that it is properly propagated to the
        // initiator request

    }
    else if (result == vp::IO_REQ_DENIED)
    {
        return result;
    }
    else
    {
        // In case of asynchronous response, the network interface will be notified by the
        // the response callback
        return result;
    }
}
