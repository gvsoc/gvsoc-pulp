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
            Jonas Martin, ETH (martinjo@student.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "floonoc.hpp"
#include "floonoc_router.hpp"
#include "floonoc_network_interface.hpp"

NetworkInterface::NetworkInterface(FlooNoc *noc, int x, int y)
    : vp::Block(noc, "ni_" + std::to_string(x) + "_" + std::to_string(y)),
      fsm_event(this, &NetworkInterface::fsm_handler), signal_narrow_req(*this, "narrow_req", 64),
      signal_wide_req(*this, "wide_req", 64)
{
    this->noc = noc;
    this->x = x;
    this->y = y;
    this->target = noc->get_target(x, y);
    this->ni_outstanding_reqs = this->noc->get_js_config()->get("ni_outstanding_reqs")->get_int();

    traces.new_trace("trace", &trace, vp::DEBUG);

    // Network interface input port
    this->narrow_input_itf.set_req_meth(&NetworkInterface::narrow_req);
    noc->new_slave_port("narrow_input_" + std::to_string(x) + "_" + std::to_string(y),
                        &this->narrow_input_itf, this);
    this->wide_input_itf.set_req_meth(&NetworkInterface::wide_req);
    noc->new_slave_port("wide_input_" + std::to_string(x) + "_" + std::to_string(y),
                        &this->wide_input_itf, this);

}

void NetworkInterface::reset(bool active)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Resetting network interface\n");
    if (active)
    {
        this->stalled = false;
        this->pending_burst_size = 0;
        this->denied_req = NULL;
        this->target_stalled = false;
        this->routers_stalled = false;
        while (this->pending_bursts.size() > 0)
        {
            this->remove_pending_burst();
        }
    }
}

int NetworkInterface::get_x()
{
    return this->x;
}

int NetworkInterface::get_y()
{
    return this->y;
}

void NetworkInterface::unstall_queue(int from_x, int from_y)
{
    // The request which was previously denied has been granted. Unstall the output queue
    // and schedule the FSM handler to check if something has to be done
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (position: (%d, %d), queue: %d)\n", from_x, from_y);
    this->stalled = false;
    this->fsm_event.enqueue();
}

vp::IoReqStatus NetworkInterface::narrow_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->signal_narrow_req = req->get_addr();
    *req->arg_get(FlooNoc::REQ_WIDE) = (void *)0;
    *req->arg_get(FlooNoc::REQ_IS_ADDRESS) = (void *)0;
    vp::IoReqStatus result = _this->req(_this, req);
    return result;
}

vp::IoReqStatus NetworkInterface::wide_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->signal_wide_req = req->get_addr();
    *req->arg_get(FlooNoc::REQ_WIDE) = (void *)1;
    *req->arg_get(FlooNoc::REQ_IS_ADDRESS) = (void *)0;
    vp::IoReqStatus result = _this->req(_this, req);
    return result;
}

// This, respectively the narrow and wide versions, should be called by the cluster (or initator of the axi burst)
vp::IoReqStatus NetworkInterface::req(vp::Block *__this, vp::IoReq *req)
{
    // This gets called when a burst is received
    NetworkInterface *_this = (NetworkInterface *)__this;

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received %s burst from initiator (burst: %p, offset: 0x%x, size: 0x%x, is_write: %d, op: %d)\n",
                     req->get_int(FlooNoc::REQ_WIDE) ? "wide" : "narrow", req, req->get_addr(), req->get_size(), req->get_is_write(), req->get_opcode());

    // We also need to push at which timestamp the burst can start being processed.
    // Since we handle it asynchronously, we need to start it only once its latency has been
    // reached
    req->set_latency(0); // Actually dont do that because the cluster sent them with some latency that doesnt make sense
    // Just enqueue it and trigger the FSM which will check if it must be processed now
    _this->add_pending_burst(req, true, _this->clock.get_cycles() + req->get_latency(), std::make_tuple(_this->x, _this->y));

    _this->fsm_event.enqueue(
        std::max((int64_t)1, _this->pending_bursts_timestamp.front() - _this->clock.get_cycles()));
    // req->set_latency(0);

    // Only accept the request if we don't have too many pending requests
    if (_this->pending_bursts.size() >= _this->ni_outstanding_reqs)
    {
        _this->denied_req = req;
        return vp::IO_REQ_DENIED;
    }
    else
    {
        return vp::IO_REQ_PENDING;
    }
}


void NetworkInterface::req_from_router(vp::IoReq *req, int from_x, int from_y)
{
    NetworkInterface *origin_ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_SRC_NI);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request from router(req: %p, base: 0x%x, size: 0x%x, isaddr: (%d), position: (%d, %d)) origin Ni: (%d, %d)\n",
                    req, req->get_addr(), req->get_size(), req->get_int(FlooNoc::REQ_IS_ADDRESS), this->x, this->y, origin_ni->get_x(), origin_ni->get_y());

    if (req->get_int(FlooNoc::REQ_IS_ADDRESS))
    {
        bool is_stalled = false;

        // Received a address request from a router.
        // Handle it by sending it to the target network interface and then sending the response packets back respecting the bandwidth of the network
        vp::IoReq *burst = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_BURST); // The burst that was stored in the request
        vp::IoMaster *target = this->target;
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Sending request to target (target name: %s)(req: %p, base: 0x%x, size: 0x%x, position: (%d, %d))\n",
                        target->get_name().c_str(), req, req->get_addr(), req->get_size(), this->x, this->y);
        // This does the actual operation(read, write or atomic operation) on the target
        // Note: Memory is read/written already here. The backward path is only used to get the delay of the network.
        vp::IoReqStatus result = target->req(req);

        if (result == vp::IO_REQ_OK)
        {
            NetworkInterface *ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_SRC_NI);
            ni->handle_response(req);
        }
        else if (result == vp::IO_REQ_DENIED)
        {
            // Store the NI in the request. Since the grant is received by top noc,
            // it will use this argument to notify the NI about the grant
            *(NetworkInterface **)req->arg_get(FlooNoc::REQ_NI) = this;
            this->target_stalled = true;
            is_stalled = true;
        }

        if(!burst->get_is_write()){
            // If the burst is a read burst, we need to send the data back to the origin
            // For a write nothing needs to be done. The sending NI will take care of it
            this->add_pending_burst(burst, false, 0, std::make_tuple(origin_ni->get_x(), origin_ni->get_y()));
            // Enqueue the FSM event to process the burst by sending the data back
            this->fsm_event.enqueue();

            // Check if the next would be denied and already notify the router. Note this is a bit hacky and misuses the vp::Req::Status but for now it should work
            if(this->pending_burst_isaddr.size() >= this->ni_outstanding_reqs)
            {
                this->trace.msg(vp::Trace::LEVEL_DEBUG, "Request denied because of too many pending requests\n");
                is_stalled = true;
            }
        }

        if (is_stalled)
        {
            // In case the access is denied, we need to stall all routers going this NI to prevent
            // any other request from arriving.
            this->noc->get_req_router(from_x, from_y)->stall_queue(this->x, this->y);
            this->noc->get_wide_router(from_x, from_y)->stall_queue(this->x, this->y);
            this->noc->get_rsp_router(from_x, from_y)->stall_queue(this->x, this->y);
            this->routers_stalled = true;
        }
    }
    else
    {
        // Received a data (non addr) request from a router.
        // Account it on the corresponding burst and notifiy the burst initator (cluster)
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Received non-addr response from router (req: %p, base: 0x%x, size: 0x%x, position: (%d, %d))\n",
                        req, req->get_addr(), req->get_size(), this->x, this->y);
        this->handle_response(req);
    }
}


void NetworkInterface::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    if (!_this->stalled)
    {
        // Check if there is a pending burst to process
        if(_this->pending_bursts.size() > 0){
            //Check if the burst is a forward going burst, or a backward going one
            if (_this->pending_burst_isaddr.front())
            {
                _this->handle_addr_req();
            }
            else
            {
                _this->handle_data_req();
            }
        }
    }

    if (_this->routers_stalled && _this->pending_bursts.size() <= _this->ni_outstanding_reqs - 1 &&
        !_this->target_stalled)
    {
        // If we removed a pending burst and the number of pending bursts was the maximum, notify the local router that it can send another request
        _this->noc->get_req_router(_this->x, _this->y)->unstall_queue(_this->x, _this->y);
        _this->noc->get_wide_router(_this->x, _this->y)->unstall_queue(_this->x, _this->y);
        _this->noc->get_rsp_router(_this->x, _this->y)->unstall_queue(_this->x, _this->y);
        _this->routers_stalled = false;
    }

    // We also have to check if another burst has been denied that can now be granted
    if (_this->denied_req && _this->pending_bursts.size() != _this->ni_outstanding_reqs)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling denied request (req: %p)\n", _this->denied_req);
        vp::IoReq *req = _this->denied_req;
        _this->denied_req = NULL;
        req->get_resp_port()->grant(req);
    }
}


void NetworkInterface::remove_pending_burst(void)
{
    this->pending_bursts.pop();
    this->pending_burst_isaddr.pop();
    this->pending_bursts_timestamp.pop();
    this->pending_bursts_origin_pos.pop();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Removing pending bursts (pending_bursts_size: %d, max: %d)\n", this->pending_bursts.size(), this->ni_outstanding_reqs);
}


void NetworkInterface::add_pending_burst(vp::IoReq *burst, bool isaddr, int64_t timestamp, std::tuple<int, int> origin_pos){
    this->pending_bursts.push(burst);
    this->pending_burst_isaddr.push(isaddr);
    this->pending_bursts_timestamp.push(timestamp);
    this->pending_bursts_origin_pos.push(origin_pos); // Also store the origin coordinates of the burst
    this->fsm_event.enqueue(); // Check if we can process the burst now
}

void NetworkInterface::handle_addr_req(void){

    if(this->pending_bursts_timestamp.front() <= this->clock.get_cycles()){
        vp::IoReq *burst = this->pending_bursts.front();
        // Get the current burst to be processed
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handling addr burst (burst: %p, offset: 0x%x, size: 0x%x, is_write: %d, op: %d)\n",
                        burst, burst->get_addr(), burst->get_size(), burst->get_is_write(), burst->get_opcode());

        vp::IoReq *req = new vp::IoReq();

        // Get base and size from current burst
        uint64_t base = burst->get_addr();
        uint64_t size = burst->get_size();

        bool wide = *(bool *)burst->arg_get(FlooNoc::REQ_WIDE);

        // Fill in the information needed by the target network interface to send back the response
        req->init();
        req->arg_alloc(FlooNoc::REQ_NB_ARGS);
        *req->arg_get(FlooNoc::REQ_SRC_NI) = (void *)this;
        *req->arg_get(FlooNoc::REQ_BURST) = (void *)burst;
        *req->arg_get(FlooNoc::REQ_IS_ADDRESS) = (void *)1;
        *req->arg_get(FlooNoc::REQ_WIDE) = (void *)wide;
        req->set_size(size);
        req->set_data(burst->get_data());
        req->set_is_write(burst->get_is_write());
        req->set_opcode(burst->get_opcode());
        req->set_second_data(burst->get_second_data());
        req->set_initiator(burst->get_initiator());
        // Get the target entry corresponding to the current base
        Entry *entry = this->noc->get_entry(base, size);

        if (entry == NULL)
        {
            // Burst is invalid if no target is found
            this->trace.msg(vp::Trace::LEVEL_ERROR, "No entry found for base 0x%x\n", base);
            return;
            burst->status = vp::IO_REQ_INVALID;

            this->remove_pending_burst();

            burst->get_resp_port()->resp(burst);
        }
        else
        {
            this->trace.msg(vp::Trace::LEVEL_TRACE, "Sending addr request to router (req: %p, base: 0x%x, size: 0x%x, destination: (%d, %d))\n",
                            req, base, size, entry->x, entry->y);

            req->set_addr(base - entry->remove_offset);
            *req->arg_get(FlooNoc::REQ_DEST_X) = (void *)(long)entry->x;
            *req->arg_get(FlooNoc::REQ_DEST_Y) = (void *)(long)entry->y;

            // Note that the router may not grant the request if its input queue is full.
            // In this case we must stall the network interface
            Router *router = this->noc->get_router(this->x, this->y, wide, req->get_is_write(), true);

            this->stalled = router->handle_request(req, this->x, this->y);
            if (this->stalled)
            {
                this->trace.msg(vp::Trace::LEVEL_TRACE, "Stalling network interface (position: (%d, %d))\n", this->x, this->y);
            }

            if (req->get_is_write())
            {
                // Modifiy the burst so it is no longer an address request and will get handled as a data request in the next cycle
                this->trace.msg(vp::Trace::LEVEL_TRACE, "Modifying burst to be a data request\n");
                this->pending_burst_isaddr.front() = false;
                this->pending_bursts_origin_pos.front() = std::make_tuple(entry->x, entry->y); // This is actually where the data will be sent back. TODO Rename this variable
            }
            else{
                this->remove_pending_burst();
            }
        }
        // Since we processed a burst, we need to check again in the next cycle if there is
        // anything to do.
        this->fsm_event.enqueue();
    }
    else{
        this->trace.msg(vp::Trace::LEVEL_TRACE, "fsm handler invoked but burst not yet ready\n");
        // If we did not handle the first pending burst because we haven't reached its
        // timestamp, schedule the event at this timestamp to be able to process it
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Enqueuing response handler for timestamp: %ld)\n", this->pending_bursts_timestamp.front());
        this->fsm_event.enqueue(
            this->pending_bursts_timestamp.front() - this->clock.get_cycles());
    }
}

void NetworkInterface::handle_data_req(void){
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Handling data burst\n");

    vp::IoReq *burst = this->pending_bursts.front();
    std::tuple<int, int> origin_pos = this->pending_bursts_origin_pos.front();
    bool wide = *(bool *)burst->arg_get(FlooNoc::REQ_WIDE);

    // If we handle the burst for the first time, we need to store the base, data and size
    if (this->pending_burst_size == 0)
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Init data burst (burst: %p, base: 0x%x, size: 0x%x, is_write: %d)\n",
                        burst, burst->get_addr(), burst->get_size(), burst->get_is_write());

        // By default, we consider the whole burst as valid. In one of the burst request is\
        // detected invalid, we will mark the whole burst as invalid
        burst->status = vp::IO_REQ_OK;
        this->pending_burst_base = burst->get_addr();
        this->pending_burst_data = burst->get_data();
        this->pending_burst_size = burst->get_size();

        // We use one data in the current burst to store the remaining size and know when the
        // last internal request has been handled to notify the end of burst
        *(int *)burst->arg_get_last() = burst->get_size();
    }



    // Size must be at max the noc width to respect the bandwidth
    uint64_t width = wide ? this->noc->wide_width : this->noc->narrow_width;
    uint64_t size = std::min(width, this->pending_burst_size);


    // Create a new request to send
    vp::IoReq *req = new vp::IoReq();
    req->init();
    req->arg_alloc(FlooNoc::REQ_NB_ARGS);
    *req->arg_get(FlooNoc::REQ_SRC_NI) = (void *)this;
    *req->arg_get(FlooNoc::REQ_BURST) = (void *)burst;
    *req->arg_get(FlooNoc::REQ_IS_ADDRESS) = (void *)0;
    *req->arg_get(FlooNoc::REQ_WIDE) = (void *)wide;
    req->set_size(size);
    req->set_is_write(burst->get_is_write());
    req->set_addr(this->pending_burst_base);

    // Store information in the request which will be needed by the routers and the target
    int src_x = std::get<0>(origin_pos);
    int src_y = std::get<1>(origin_pos);
    *req->arg_get(FlooNoc::REQ_DEST_X) = (void *)(long)src_x;
    *req->arg_get(FlooNoc::REQ_DEST_Y) = (void *)(long)src_y;


    // Update the current burst for next request
    this->pending_burst_base += size;
    this->pending_burst_data += size;
    this->pending_burst_size -= size;

    // And remove the burst if all requests were sent. Note that this will allow next burst
    // to be processed even though some requests may still be on-going for it.
    if (this->pending_burst_size == 0)
    {
        this->remove_pending_burst();
    }

    // And forward to the first router which is at the same position as the network
    // interface
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Sending data request to router (req: %p, base: %x, size: %x, destination: (%d, %d))\n",
                    req, req->get_addr(), size, src_x, src_y);
    // Note that the router may not grant the request if its input queue is full.
    // In this case we must stall the network interface

    Router *router = this->noc->get_router(this->x, this->y, wide, req->get_is_write(), false);

    this->stalled = router->handle_request(req, this->x, this->y);

    if (this->stalled)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Stalling network interface (position: (%d, %d))\n", this->x, this->y);
    }

    // Check in next cycle if there is something to do
    this->fsm_event.enqueue();
}


void NetworkInterface::handle_response(vp::IoReq *req)
{
    // This gets called by the routers when an internal request has been handled
    // First extract the corresponding burst from the request so that we can update the burst.
    vp::IoReq *burst = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_BURST);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request response (req: %p)\n", req);

    // If at least one of the request is invalid, this makes the whole burst invalid
    if (req->status == vp::IO_REQ_INVALID)
    {
        burst->status = vp::IO_REQ_INVALID;
    }

    if (req->get_int(FlooNoc::REQ_IS_ADDRESS))
    {
        burst->set_int(FlooNoc::REQ_IS_ADDRESS, burst->get_int(FlooNoc::REQ_IS_ADDRESS) + 1);
        if (burst->get_int(FlooNoc::REQ_IS_ADDRESS) == 2)
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished %s burst (burst: %p, latency: %d, phase: %d)\n", burst->get_int(FlooNoc::REQ_WIDE) ? "wide" : "narrow", burst, burst->get_latency(), burst->get_int(FlooNoc::REQ_IS_ADDRESS));
            burst->get_resp_port()->resp(burst);
        }
    }
    else
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Reducing remaining size of burst (burst: %p, size: %d, req: %p, size %d)\n",
                        burst, *(int *)burst->arg_get_last(), req, req->get_size());
        // Account the received response on the burst
        *(int *)burst->arg_get_last() -= req->get_size();
        // And respond to it if all responses have been received
        if (*(int *)burst->arg_get_last() == 0)
        {
            burst->set_int(FlooNoc::REQ_IS_ADDRESS, burst->get_int(FlooNoc::REQ_IS_ADDRESS) + 1);
            if (burst->get_int(FlooNoc::REQ_IS_ADDRESS) == 2)
            {
                this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished %s burst (burst: %p, phase: %d)\n", burst->get_int(FlooNoc::REQ_WIDE) ? "wide" : "narrow", burst, burst->get_int(FlooNoc::REQ_IS_ADDRESS));
                burst->get_resp_port()->resp(burst);
            }
        }
    }

    // Delete the request since we don't need it anymore
    delete req;
    // Trigger the FSM since something may need to be done now that a new request is available
    this->fsm_event.enqueue();
}

void NetworkInterface::grant(vp::IoReq *req)
{
    this->target_stalled = false;
    this->fsm_event.enqueue();
}
