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

#include <string>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "floonoc.hpp"
#include "floonoc_router.hpp"
#include "floonoc_network_interface.hpp"

NetworkQueue::NetworkQueue(NetworkInterface &ni, std::string name, uint64_t width, bool is_wide)
: FloonocNode(&ni, name), ni(ni), width(width), is_wide(is_wide)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
}

void NetworkQueue::reset(bool active)
{
    if (active)
    {
        while (this->queue.size() > 0)
        {
            this->queue.pop();
        }

        this->stalled = false;
    }
}


void NetworkQueue::check()
{
    // Check if we can send a pending request to the router
    if (!this->stalled && this->queue.size() > 0)
    {
        this->send_router_req();
    }
}

void NetworkQueue::unstall_queue(int from_x, int from_y)
{
    // The request which was previously denied has been granted. Unstall the output queue
    // and schedule the FSM handler to check if something has to be done
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (position: (%d, %d), queue: %d)\n", from_x, from_y);
    this->stalled = false;
    this->ni.fsm_event.enqueue();
}

// This, respectively the narrow and wide versions, should be called by the cluster (or initator of the axi burst)
void NetworkQueue::handle_req(vp::IoReq *req)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received %s burst from initiator (burst: %p, offset: 0x%x, size: 0x%x, is_write: %d, op: %d)\n",
                     req->get_int(FlooNoc::REQ_WIDE) ? "wide" : "narrow", req, req->get_addr(), req->get_size(), req->get_is_write(), req->get_opcode());

    this->enqueue_router_req(req, true, true);
    if (req->get_is_write())
    {
        this->enqueue_router_req(req, false, true);
    }
}

void NetworkQueue::handle_rsp(vp::IoReq *req, bool is_address)
{
    this->enqueue_router_req(req, is_address, false);
}

bool NetworkQueue::handle_request(FloonocNode *node, vp::IoReq *req, int from_x, int from_y)
{
    return true;
}


void NetworkQueue::enqueue_router_req(vp::IoReq *req, bool is_address, bool is_req)
{
    uint64_t burst_base = req->get_addr();
    uint64_t burst_size = req->get_size();
    uint8_t *burst_data = req->get_data();
    bool wide = *(bool *)req->arg_get(FlooNoc::REQ_WIDE);

    while(burst_size > 0)
    {
        uint64_t size = is_address ? burst_size : std::min(this->width, burst_size);
        vp::IoReq *router_req = new vp::IoReq();

        // Fill in the information needed by the target network interface to send back the response
        router_req->init();
        router_req->arg_alloc(FlooNoc::REQ_NB_ARGS);
        *router_req->arg_get(FlooNoc::REQ_SRC_NI) = (void *)&this->ni;
        *router_req->arg_get(FlooNoc::REQ_BURST) = (void *)req;
        *router_req->arg_get(FlooNoc::REQ_IS_ADDRESS) = (void *)is_address;
        *router_req->arg_get(FlooNoc::REQ_WIDE) = (void *)wide;
        router_req->set_size(size);
        router_req->set_data(burst_data);
        router_req->set_addr(burst_base);
        router_req->set_is_write(req->get_is_write());
        router_req->set_opcode(req->get_opcode());
        router_req->set_second_data(req->get_second_data());

        if (is_req)
        {
            if (wide)
            {
                req->get_is_write() ? this->ni.wide_write_pending_burst_nb_req++ :
                    this->ni.wide_read_pending_burst_nb_req++;
            }
            else
            {
                req->get_is_write() ? this->ni.narrow_write_pending_burst_nb_req++ :
                    this->ni.narrow_read_pending_burst_nb_req++;
            }

            Entry *entry = this->ni.noc->get_entry(burst_base, size);

            if (entry == NULL)
            {
                // Burst is invalid if no target is found
                this->trace.msg(vp::Trace::LEVEL_ERROR, "No entry found for base 0x%x\n", burst_base);
                return;
                // TODO
                // burst->status = vp::IO_REQ_INVALID;

                // this->remove_pending_burst();

                // burst->get_resp_port()->resp(burst);
            }
            else
            {
                // Be careful to not have any request which is crossing 2 entries
                uint64_t max_size = entry->base + entry->size - burst_base;
                size = std::min(max_size, size);

                this->trace.msg(vp::Trace::LEVEL_TRACE,
                    "Enqueue request to router (req: %p, base: 0x%x, size: 0x%x, "
                    "destination: (%d, %d))\n",
                    router_req, burst_base, size, entry->x, entry->y);

                router_req->set_size(size);
                router_req->set_addr(burst_base - entry->remove_offset);
                router_req->initiator_addr = burst_base;
                *router_req->arg_get(FlooNoc::REQ_DEST_X) = (void *)(long)entry->x;
                *router_req->arg_get(FlooNoc::REQ_DEST_Y) = (void *)(long)entry->y;

            }
        }
        else
        {
            *router_req->arg_get(FlooNoc::REQ_SRC_NI) = NULL;
            *router_req->arg_get(FlooNoc::REQ_DEST_X) = *req->arg_get(FlooNoc::REQ_DEST_X);
            *router_req->arg_get(FlooNoc::REQ_DEST_Y) = *req->arg_get(FlooNoc::REQ_DEST_Y);
            *router_req->arg_get(FlooNoc::REQ_BURST) = *req->arg_get(FlooNoc::REQ_BURST);
        }

        this->queue.push(router_req);

        burst_base += size;
        burst_data += size;
        burst_size -= size;
    }
    this->ni.fsm_event.enqueue();
}

void NetworkQueue::send_router_req()
{
    vp::IoReq *req = this->queue.front();
    this->queue.pop();
    vp::IoReq *burst = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_BURST);

    if (*(NetworkInterface **)req->arg_get(FlooNoc::REQ_SRC_NI))
    {
        int *nb_req;
       if (*(bool *)req->arg_get(FlooNoc::REQ_WIDE))
       {
           nb_req = burst->get_is_write() ? &this->ni.wide_write_pending_burst_nb_req :
               &this->ni.wide_read_pending_burst_nb_req;
       }
       else
       {
           nb_req = burst->get_is_write() ? &this->ni.narrow_write_pending_burst_nb_req :
               &this->ni.narrow_read_pending_burst_nb_req;
       }
        *nb_req = *nb_req - 1;
        if (*nb_req == 0)
        {
            this->ni.fsm_event.enqueue();
        }
    }

    // Get the current burst to be processed
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handling addr burst (burst: %p, offset: 0x%x, size: 0x%x, is_write: %d, op: %d)\n",
                    burst, burst->get_addr(), burst->get_size(), burst->get_is_write(), burst->get_opcode());

    // Note that the router may not grant the request if its input queue is full.
    // In this case we must stall the network interface
    this->stalled = this->router->handle_request(this, req, this->ni.x, this->ni.y);
    if (this->stalled)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Stalling network interface (position: (%d, %d))\n", this->ni.x, this->ni.y);
    }

    // Since we processed a burst, we need to check again in the next cycle if there is
    // anything to do.
    if (this->queue.size() > 0)
    {
        this->ni.fsm_event.enqueue();
    }
}

NetworkInterface::NetworkInterface(FlooNoc *noc, int x, int y, std::string itf_name)
    : FloonocNode(noc, "ni_" + std::to_string(x) + "_" + std::to_string(y)),
      fsm_event(this, &NetworkInterface::fsm_handler), signal_narrow_req(*this, "narrow_req", 64),
      signal_wide_req(*this, "wide_req", 64),
      req_queue(*this, "narrow", noc->narrow_width, false),
      rsp_queue(*this, "rsp", noc->narrow_width, false),
      wide_queue(*this, "wide", noc->wide_width, true),
      response_queue(this, "response_queue", &this->fsm_event)
{
    this->noc = noc;
    this->x = x;
    this->y = y;

    this->wide_output_itf.set_resp_meth(&NetworkInterface::wide_response);
    this->wide_output_itf.set_grant_meth(&NetworkInterface::wide_grant);
    noc->new_master_port("ni_wide_" + std::to_string(x) + "_" + std::to_string(y),
        &this->wide_output_itf, this);

    this->narrow_output_itf.set_resp_meth(&NetworkInterface::narrow_response);
    this->narrow_output_itf.set_grant_meth(&NetworkInterface::narrow_grant);
    noc->new_master_port("ni_narrow_" + std::to_string(x) + "_" + std::to_string(y),
        &this->narrow_output_itf, this);

    traces.new_trace("trace", &trace, vp::DEBUG);

    // Network interface input port
    this->narrow_input_itf.set_req_meth(&NetworkInterface::narrow_req);
    noc->new_slave_port("narrow_input_" + std::to_string(x) + "_" + std::to_string(y),
                        &this->narrow_input_itf, this);
    this->wide_input_itf.set_req_meth(&NetworkInterface::wide_req);
    noc->new_slave_port("wide_input_" + std::to_string(x) + "_" + std::to_string(y),
                        &this->wide_input_itf, this);

    this->ni_outstanding_reqs = this->noc->get_js_config()->get("ni_outstanding_reqs")->get_int();
}

void NetworkInterface::set_router(int nw, Router *router)
{
    if (router == NULL)
    {
        this->trace.fatal("No router found for network interface (nw: %s)\n",
            nw == NW_REQ ? "req" : nw == NW_RSP ? "rsp" : "wide");
    }

    this->router[nw] = router;
    switch (nw)
    {
        case NW_REQ: this->req_queue.router = router;
        case NW_RSP: this->rsp_queue.router = router;
        case NW_WIDE: this->wide_queue.router = router;
    }
}

// This gets called when a request was pending and the response is received
void NetworkInterface::wide_response(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->handle_response(req);
}

// This gets called after a request sent to a target was denied, and it is now granted
void NetworkInterface::wide_grant(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->grant(req);
}

// This gets called when a request was pending and the response is received
void NetworkInterface::narrow_response(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->handle_response(req);
}

// This gets called after a request sent to a target was denied, and it is now granted
void NetworkInterface::narrow_grant(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->grant(req);
}

void NetworkInterface::reset(bool active)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Resetting network interface\n");
    if (active)
    {
        this->target_stalled = false;
        this->routers_stalled = NULL;
        this->wide_read_pending_burst = NULL;
        this->wide_write_pending_burst = NULL;
        this->wide_read_pending_burst_nb_req = 0;
        this->wide_write_pending_burst_nb_req = 0;
        this->narrow_read_pending_burst = NULL;
        this->narrow_write_pending_burst = NULL;
        this->narrow_read_pending_burst_nb_req = 0;
        this->narrow_write_pending_burst_nb_req = 0;
        this->nb_pending_bursts[0] = 0;
        this->nb_pending_bursts[1] = 0;
    }
}

int NetworkInterface::get_req_nw(bool is_wide, bool is_write)
{
    if (is_wide)
    {
        return is_write ? NetworkInterface::NW_WIDE : NetworkInterface::NW_REQ;
    }
    else
    {
        return NetworkInterface::NW_REQ;
    }
}

int NetworkInterface::get_rsp_nw(bool is_wide, bool is_write)
{
    return is_wide ? NetworkInterface::NW_WIDE : NetworkInterface::NW_RSP;
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
}

vp::IoReqStatus NetworkInterface::narrow_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->signal_narrow_req = req->get_addr();
    *req->arg_get(FlooNoc::REQ_WIDE) = (void *)0;
    return _this->handle_req(req);
}

vp::IoReqStatus NetworkInterface::wide_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->signal_wide_req = req->get_addr();
    *req->arg_get(FlooNoc::REQ_WIDE) = (void *)1;
    return _this->handle_req(req);
}

vp::IoReqStatus NetworkInterface::handle_req(vp::IoReq *req)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request from target (req: %p, base: 0x%x, size: 0x%x, wide: %d)\n",
        req, req->get_addr(), req->get_size(), *(vp::IoReq **)req->arg_get(FlooNoc::REQ_WIDE));

    // We use one data in the current burst to store the remaining size and know when the
    // last internal request has been handled to notify the end of burst
    *(int *)req->arg_get_last() = req->get_size();

    vp::IoReq **queue;
    std::queue<vp::IoReq *> *denied_queue;
    bool is_wide = *(bool *)req->arg_get(FlooNoc::REQ_WIDE);
    if (is_wide)
    {
        queue = req->get_is_write() ? &this->wide_write_pending_burst :
                &this->wide_read_pending_burst;
        denied_queue = req->get_is_write() ? &this->wide_denied_write_req :
            &this->wide_denied_read_req;
    }
    else
    {
        queue = req->get_is_write() ? &this->narrow_write_pending_burst :
                &this->narrow_read_pending_burst;
        denied_queue = req->get_is_write() ? &this->narrow_denied_write_req :
            &this->narrow_denied_read_req;
    }

    if (*queue || this->nb_pending_bursts[is_wide] >= this->ni_outstanding_reqs)
    {
        denied_queue->push(req);
        return vp::IO_REQ_DENIED;
    }
    else
    {
        this->nb_pending_bursts[is_wide]++;
        *queue = req;
        if (!req->get_is_write() || !*(vp::IoReq **)req->arg_get(FlooNoc::REQ_WIDE))
        {
            this->req_queue.handle_req(req);
        }
        else
        {
            this->wide_queue.handle_req(req);
        }
        this->fsm_event.enqueue();
        return vp::IO_REQ_PENDING;
    }
}

bool NetworkInterface::handle_request(FloonocNode *node, vp::IoReq *req, int from_x, int from_y)
{
    NetworkInterface *origin_ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_SRC_NI);

    if (origin_ni == NULL)
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received response from router (req: %p)\n", req);

        vp::IoReq *burst = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_BURST);
        bool wide = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_WIDE);

        if (burst->get_is_write())
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received write burst response (burst: %p)\n",
                burst);
            this->nb_pending_bursts[wide]--;

            if (this->nb_pending_bursts[wide] < 0) abort();

            burst->get_resp_port()->resp(burst);
        }
        else
        {
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "Reducing remaining size of burst (burst: %p, size: %d, req: %p, size %d)\n",
                burst, *(int *)burst->arg_get_last(), req, req->get_size());
            *(int *)burst->arg_get_last() -= req->get_size();

            if (*(int *)burst->arg_get_last() == 0)
            {
                this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished burst (burst: %p)\n", burst);
                this->nb_pending_bursts[wide]--;
                if (this->nb_pending_bursts[wide] < 0) abort();
                burst->get_resp_port()->resp(burst);
            }
        }

        this->fsm_event.enqueue();

        delete req;
    }
    else
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Received request from router (req: %p, base: 0x%x, size: 0x%x, isaddr: (%d), "
            "position: (%d, %d)) origin Ni: (%d, %d)\n",
            req, req->get_addr(), req->get_size(), req->get_int(FlooNoc::REQ_IS_ADDRESS), this->x,
            this->y, origin_ni->get_x(), origin_ni->get_y());

        if ((req->get_is_write() && !req->get_int(FlooNoc::REQ_IS_ADDRESS)) || !req->get_is_write())
        {
            bool is_stalled = false;

            // Received a address request from a router.
            // Handle it by sending it to the target network interface and then sending the response
            // packets back respecting the bandwidth of the network
            bool wide = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_WIDE);
            vp::IoMaster *target = wide ? &this->wide_output_itf : &this->narrow_output_itf;
            this->trace.msg(vp::Trace::LEVEL_DEBUG,
                "Sending request to target (req: %p, base: 0x%x, size: 0x%x)\n",
                req, req->get_addr(), req->get_size());
            // This does the actual operation(read, write or atomic operation) on the target
            // Note: Memory is read/written already here. The backward path is only used to get the delay of the network.
            vp::IoReqStatus result = target->req(req);

            if (result == vp::IO_REQ_OK)
            {
                if (req->get_latency() > 0)
                {
                    this->response_queue.push_delayed(req, req->get_latency());
                }
                else
                {
                    this->handle_response(req);
                }
            }
            else if (result == vp::IO_REQ_DENIED)
            {
                // Store the NI in the request. Since the grant is received by top noc,
                // it will use this argument to notify the NI about the grant
                this->target_stalled = true;
                is_stalled = true;
            }

            if (is_stalled)
            {
                this->routers_stalled = node;
            }
            return is_stalled;
        }
        else
        {
            delete req;
        }
    }

    return false;
}


void NetworkInterface::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    NetworkInterface *_this = (NetworkInterface *)__this;

    _this->req_queue.check();
    _this->rsp_queue.check();
    _this->wide_queue.check();



    if (!_this->response_queue.empty())
    {
        _this->handle_response((vp::IoReq *)_this->response_queue.pop());
    }

    if (_this->routers_stalled && !_this->target_stalled)
    {
        // If we removed a pending burst and the number of pending bursts was the maximum, notify the local router that it can send another request
        _this->routers_stalled->unstall_queue(_this->x, _this->y);
        _this->routers_stalled = NULL;
    }

    if (_this->wide_read_pending_burst && _this->wide_read_pending_burst_nb_req == 0)
    {
        _this->wide_read_pending_burst = NULL;
        _this->fsm_event.enqueue();
    }

    if (_this->wide_write_pending_burst && _this->wide_write_pending_burst_nb_req == 0)
    {
        _this->wide_write_pending_burst = NULL;
        _this->fsm_event.enqueue();
    }

    if (_this->narrow_read_pending_burst && _this->narrow_read_pending_burst_nb_req == 0)
    {
        _this->narrow_read_pending_burst = NULL;
        _this->fsm_event.enqueue();
    }

    if (_this->narrow_write_pending_burst && _this->narrow_write_pending_burst_nb_req == 0)
    {
        _this->narrow_write_pending_burst = NULL;
        _this->fsm_event.enqueue();
    }

    // We also have to check if another burst has been denied that can now be granted
    if (_this->wide_denied_read_req.size() > 0 && _this->wide_read_pending_burst == NULL && _this->nb_pending_bursts[1] < _this->ni_outstanding_reqs)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling denied request (req: %p)\n", _this->wide_denied_read_req.front());
        vp::IoReq *req = _this->wide_denied_read_req.front();
        _this->nb_pending_bursts[1]++;
        _this->wide_read_pending_burst = req;
        _this->wide_denied_read_req.pop();
        if (!req->get_is_write())
        {
            _this->req_queue.handle_req(req);
        }
        else
        {
            _this->wide_queue.handle_req(req);
        }
        req->get_resp_port()->grant(req);
        _this->fsm_event.enqueue();
    }

    if (_this->wide_denied_write_req.size() > 0 && _this->wide_write_pending_burst == NULL && _this->nb_pending_bursts[1] < _this->ni_outstanding_reqs)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling denied request (req: %p)\n", _this->wide_denied_write_req.front());
        vp::IoReq *req = _this->wide_denied_write_req.front();
        _this->nb_pending_bursts[1]++;
        _this->wide_write_pending_burst = req;
        _this->wide_denied_write_req.pop();
        if (!req->get_is_write())
        {
            _this->req_queue.handle_req(req);
        }
        else
        {
            _this->wide_queue.handle_req(req);
        }
        req->get_resp_port()->grant(req);
        _this->fsm_event.enqueue();
    }

    // We also have to check if another burst has been denied that can now be granted
    if (_this->narrow_denied_read_req.size() > 0 && _this->narrow_read_pending_burst == NULL && _this->nb_pending_bursts[0] < _this->ni_outstanding_reqs)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling denied request (req: %p)\n", _this->narrow_denied_read_req.front());
        vp::IoReq *req = _this->narrow_denied_read_req.front();
        _this->nb_pending_bursts[0]++;
        _this->narrow_read_pending_burst = req;
        _this->narrow_denied_read_req.pop();
        _this->req_queue.handle_req(req);
        req->get_resp_port()->grant(req);
        _this->fsm_event.enqueue();
    }

    if (_this->narrow_denied_write_req.size() > 0 && _this->narrow_write_pending_burst == NULL && _this->nb_pending_bursts[0] < _this->ni_outstanding_reqs)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling denied request (req: %p)\n", _this->narrow_denied_write_req.front());
        vp::IoReq *req = _this->narrow_denied_write_req.front();
        _this->nb_pending_bursts[0]++;
        _this->narrow_write_pending_burst = req;
        _this->narrow_denied_write_req.pop();
        _this->req_queue.handle_req(req);
        req->get_resp_port()->grant(req);
        _this->fsm_event.enqueue();
    }

    _this->response_queue.trigger_next();
}


void NetworkInterface::handle_response(vp::IoReq *req)
{
    if (!req->get_is_write())
    {
        NetworkInterface *origin_ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_SRC_NI);

        *req->arg_get(FlooNoc::REQ_SRC_NI) = NULL;
        *req->arg_get(FlooNoc::REQ_DEST_X) = (void *)(long)origin_ni->x;
        *req->arg_get(FlooNoc::REQ_DEST_Y) = (void *)(long)origin_ni->y;
        if (*(bool *)req->arg_get(FlooNoc::REQ_WIDE))
        {
            this->wide_queue.handle_rsp(req, false);
        }
        else
        {
            this->rsp_queue.handle_rsp(req, false);
        }
    }
    else
    {
        vp::IoReq *burst = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_BURST);

        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Reducing remaining size of burst (burst: %p, size: %d, req: %p, size %d)\n",
            burst, *(int *)burst->arg_get_last(), req, req->get_size());
        *(int *)burst->arg_get_last() -= req->get_size();

        if (*(int *)burst->arg_get_last() == 0)
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished burst (burst: %p)\n", burst);
            NetworkInterface *origin_ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_SRC_NI);

            *req->arg_get(FlooNoc::REQ_SRC_NI) = NULL;
            *req->arg_get(FlooNoc::REQ_DEST_X) = (void *)(long)origin_ni->x;
            *req->arg_get(FlooNoc::REQ_DEST_Y) = (void *)(long)origin_ni->y;
            this->rsp_queue.handle_rsp(req, true);
        }
    }
    delete req;
}

void NetworkInterface::grant(vp::IoReq *req)
{
    this->target_stalled = false;
    this->fsm_event.enqueue();
}
