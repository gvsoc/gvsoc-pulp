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

#include <string>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "floonoc_v2.hpp"
#include "floonoc_router_v2.hpp"
#include "floonoc_network_interface_v2.hpp"

NetworkQueueV2::NetworkQueueV2(NetworkInterfaceV2 &ni, std::string name, uint64_t width, bool is_wide)
: FloonocNodeV2(&ni, name), ni(ni), width(width), is_wide(is_wide)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
}

void NetworkQueueV2::reset(bool active)
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

void NetworkQueueV2::check()
{
    if (!this->stalled && this->queue.size() > 0)
    {
        this->send_router_req();
    }
}

void NetworkQueueV2::unstall_queue(int from_x, int from_y)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (position: (%d, %d))\n", from_x, from_y);
    this->stalled = false;
    this->ni.fsm_event.enqueue();
}

void NetworkQueueV2::handle_req(vp::IoReq *req, bool wide)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received %s burst from initiator (burst: %p, offset: 0x%x, size: 0x%x, is_write: %d, op: %d)\n",
                     wide ? "wide" : "narrow", req, req->get_addr(), req->get_size(), req->get_is_write(), req->get_opcode());

    // wide here is the EXTERNAL burst's wide flag (which port it came in on),
    // independent of which internal NetworkQueueV2 (req/rsp/wide) carries the
    // router_reqs. The destination NI uses router_req->wide to pick which
    // target (wide_output_itf vs narrow_output_itf) to forward to, so it must
    // match the input port, not the carrier network.
    this->enqueue_router_req(req, true, wide, true);
    if (req->get_is_write())
    {
        this->enqueue_router_req(req, false, wide, true);
    }
}

void NetworkQueueV2::handle_rsp(FloonocReqV2 *req, bool is_address)
{
    this->enqueue_router_rsp(req, is_address);
}

bool NetworkQueueV2::handle_request(FloonocNodeV2 *node, FloonocReqV2 *req, int from_x, int from_y)
{
    return true;
}


void NetworkQueueV2::enqueue_router_req(vp::IoReq *req, bool is_address, bool wide, bool is_req)
{
    uint64_t burst_base = req->get_addr();
    uint64_t burst_size = req->get_size();
    uint8_t *burst_data = req->get_data();

    while(burst_size > 0)
    {
        uint64_t size = is_address ? burst_size : std::min(this->width, burst_size);
        FloonocReqV2 *router_req = new FloonocReqV2();

        router_req->prepare();
        router_req->src_ni = &this->ni;
        router_req->burst = req;
        router_req->is_address = is_address;
        router_req->wide = wide;
        router_req->set_size(size);
        router_req->set_data(burst_data);
        router_req->set_addr(burst_base);
        router_req->set_is_write(req->get_is_write());
        router_req->set_opcode(req->get_opcode());
        router_req->set_second_data(req->get_second_data());

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

        EntryV2 *entry = this->ni.noc->get_entry(burst_base, size);

        if (entry == NULL)
        {
            this->trace.msg(vp::Trace::LEVEL_ERROR, "No entry found for base 0x%x\n", burst_base);
            return;
        }
        else
        {
            uint64_t max_size = entry->base + entry->size - burst_base;
            size = std::min(max_size, size);

            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "Enqueue request to router (req: %p, base: 0x%x, size: 0x%x, "
                "destination: (%d, %d))\n",
                router_req, burst_base, size, entry->x, entry->y);

            router_req->set_size(size);
            router_req->set_addr(burst_base - entry->remove_offset);
            router_req->initiator_addr = burst_base;
            router_req->dest_x = entry->x;
            router_req->dest_y = entry->y;
        }

        this->queue.push(router_req);

        burst_base += size;
        burst_data += size;
        burst_size -= size;
    }
    this->ni.fsm_event.enqueue();
}

void NetworkQueueV2::enqueue_router_rsp(FloonocReqV2 *req, bool is_address)
{
    // Allocate a fresh FloonocReqV2 for the response traversal, copying the
    // metadata the router needs from the incoming request. The caller deletes
    // the original after we return — mirrors the v1 enqueue_router_req
    // response-path branch.
    FloonocReqV2 *router_req = new FloonocReqV2();

    router_req->prepare();
    router_req->src_ni = NULL;
    router_req->burst = req->burst;
    router_req->is_address = is_address;
    router_req->wide = req->wide;
    router_req->dest_x = req->dest_x;
    router_req->dest_y = req->dest_y;
    router_req->initiator_addr = req->initiator_addr;
    router_req->set_size(req->get_size());
    router_req->set_data(req->get_data());
    router_req->set_addr(req->get_addr());
    router_req->set_is_write(req->get_is_write());
    router_req->set_opcode(req->get_opcode());
    router_req->set_second_data(req->get_second_data());

    this->queue.push(router_req);
    this->ni.fsm_event.enqueue();
}

void NetworkQueueV2::send_router_req()
{
    FloonocReqV2 *req = this->queue.front();
    this->queue.pop();
    vp::IoReq *burst = req->burst;

    if (req->src_ni)
    {
        int *nb_req;
        if (req->wide)
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

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handling addr burst (burst: %p, offset: 0x%x, size: 0x%x, is_write: %d, op: %d)\n",
                    burst, burst->get_addr(), burst->get_size(), burst->get_is_write(), burst->get_opcode());

    this->stalled = this->router->handle_request(this, req, this->ni.x, this->ni.y);
    if (this->stalled)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Stalling network interface (position: (%d, %d))\n", this->ni.x, this->ni.y);
    }

    if (this->queue.size() > 0)
    {
        this->ni.fsm_event.enqueue();
    }
}

NetworkInterfaceV2::NetworkInterfaceV2(FlooNocV2 *noc, int x, int y, std::string itf_name)
    : FloonocNodeV2(noc, "ni_" + std::to_string(x) + "_" + std::to_string(y)),
      wide_output_itf(&NetworkInterfaceV2::wide_retry, &NetworkInterfaceV2::wide_response),
      narrow_output_itf(&NetworkInterfaceV2::narrow_retry, &NetworkInterfaceV2::narrow_response),
      wide_input_itf(&NetworkInterfaceV2::wide_req),
      narrow_input_itf(&NetworkInterfaceV2::narrow_req),
      fsm_event(this, &NetworkInterfaceV2::fsm_handler),
      signal_narrow_req(*this, "narrow_req", 64),
      signal_wide_req(*this, "wide_req", 64),
      req_queue(*this, "narrow", noc->narrow_width, false),
      rsp_queue(*this, "rsp", noc->narrow_width, false),
      wide_queue(*this, "wide", noc->wide_width, true),
      response_queue(this, "response_queue", &this->fsm_event)
{
    this->noc = noc;
    this->x = x;
    this->y = y;

    noc->new_master_port("ni_wide_" + std::to_string(x) + "_" + std::to_string(y),
        &this->wide_output_itf, this);
    noc->new_master_port("ni_narrow_" + std::to_string(x) + "_" + std::to_string(y),
        &this->narrow_output_itf, this);

    traces.new_trace("trace", &trace, vp::DEBUG);

    noc->new_slave_port("narrow_input_" + std::to_string(x) + "_" + std::to_string(y),
                        &this->narrow_input_itf, this);
    noc->new_slave_port("wide_input_" + std::to_string(x) + "_" + std::to_string(y),
                        &this->wide_input_itf, this);

    this->ni_outstanding_reqs = this->noc->get_js_config()->get("ni_outstanding_reqs")->get_int();
}

void NetworkInterfaceV2::set_router(int nw, RouterV2 *router)
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

void NetworkInterfaceV2::wide_response(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    _this->handle_response((FloonocReqV2 *)req);
}

void NetworkInterfaceV2::wide_retry(vp::Block *__this)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    // The downstream target is ready again. Re-send the req we were holding
    // and unstall the upstream router so further reqs can flow.
    if (_this->wide_target_stalled_req)
    {
        FloonocReqV2 *req = _this->wide_target_stalled_req;
        _this->wide_target_stalled_req = NULL;

        req->prepare();
        vp::IoReqStatus result = _this->wide_output_itf.req(req);
        if (result == vp::IO_REQ_DENIED)
        {
            // Target denied again — hold and wait for next retry.
            _this->wide_target_stalled_req = req;
            return;
        }
        else if (result == vp::IO_REQ_DONE)
        {
            if (req->get_latency() > 0)
            {
                _this->response_queue.push_delayed(req, req->get_latency());
            }
            else
            {
                _this->handle_response(req);
            }
        }
        // GRANTED: async response will arrive via wide_response.

        if (_this->wide_routers_stalled)
        {
            _this->wide_routers_stalled->unstall_queue(_this->x, _this->y);
            _this->wide_routers_stalled = NULL;
        }
    }
    _this->target_stalled = false;
    _this->fsm_event.enqueue();
}

void NetworkInterfaceV2::narrow_response(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    _this->handle_response((FloonocReqV2 *)req);
}

void NetworkInterfaceV2::narrow_retry(vp::Block *__this)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    if (_this->narrow_target_stalled_req)
    {
        FloonocReqV2 *req = _this->narrow_target_stalled_req;
        _this->narrow_target_stalled_req = NULL;

        req->prepare();
        vp::IoReqStatus result = _this->narrow_output_itf.req(req);
        if (result == vp::IO_REQ_DENIED)
        {
            _this->narrow_target_stalled_req = req;
            return;
        }
        else if (result == vp::IO_REQ_DONE)
        {
            if (req->get_latency() > 0)
            {
                _this->response_queue.push_delayed(req, req->get_latency());
            }
            else
            {
                _this->handle_response(req);
            }
        }

        if (_this->narrow_routers_stalled)
        {
            _this->narrow_routers_stalled->unstall_queue(_this->x, _this->y);
            _this->narrow_routers_stalled = NULL;
        }
    }
    _this->target_stalled = false;
    _this->fsm_event.enqueue();
}

void NetworkInterfaceV2::reset(bool active)
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
        this->owes_retry_wide_input = false;
        this->owes_retry_narrow_input = false;
        this->wide_target_stalled_req = NULL;
        this->narrow_target_stalled_req = NULL;
        this->wide_routers_stalled = NULL;
        this->narrow_routers_stalled = NULL;
    }
}

int NetworkInterfaceV2::get_req_nw(bool is_wide, bool is_write)
{
    if (is_wide)
    {
        return is_write ? NetworkInterfaceV2::NW_WIDE : NetworkInterfaceV2::NW_REQ;
    }
    else
    {
        return NetworkInterfaceV2::NW_REQ;
    }
}

int NetworkInterfaceV2::get_rsp_nw(bool is_wide, bool is_write)
{
    return is_wide ? NetworkInterfaceV2::NW_WIDE : NetworkInterfaceV2::NW_RSP;
}

int NetworkInterfaceV2::get_x()
{
    return this->x;
}

int NetworkInterfaceV2::get_y()
{
    return this->y;
}

void NetworkInterfaceV2::unstall_queue(int from_x, int from_y)
{
}

vp::IoReqStatus NetworkInterfaceV2::narrow_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    _this->signal_narrow_req = req->get_addr();
    return _this->handle_req(req, /*wide=*/false);
}

vp::IoReqStatus NetworkInterfaceV2::wide_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    _this->signal_wide_req = req->get_addr();
    return _this->handle_req(req, /*wide=*/true);
}

vp::IoReqStatus NetworkInterfaceV2::handle_req(vp::IoReq *req, bool wide)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request from target (req: %p, base: 0x%x, size: 0x%x, wide: %d)\n",
        req, req->get_addr(), req->get_size(), wide);

    // Use the v2 IoReq remaining_size field to track how many bytes still need
    // to be returned through the mesh before we can ack the burst.
    req->remaining_size = req->get_size();
    // Remember which external port to use for the response (single field of
    // vp::IoReq we can repurpose without subclassing the external request).
    req->initiator = wide ? (void *)&this->wide_input_itf
                          : (void *)&this->narrow_input_itf;

    vp::IoReq **queue;
    if (wide)
    {
        queue = req->get_is_write() ? &this->wide_write_pending_burst :
                &this->wide_read_pending_burst;
    }
    else
    {
        queue = req->get_is_write() ? &this->narrow_write_pending_burst :
                &this->narrow_read_pending_burst;
    }

    if (*queue || this->nb_pending_bursts[wide] >= this->ni_outstanding_reqs)
    {
        // v2 deny: do not queue. Remember that the master is owed a retry()
        // when capacity returns.
        if (wide)
        {
            this->owes_retry_wide_input = true;
        }
        else
        {
            this->owes_retry_narrow_input = true;
        }
        return vp::IO_REQ_DENIED;
    }
    else
    {
        this->nb_pending_bursts[wide]++;
        *queue = req;
        if (!req->get_is_write() || !wide)
        {
            this->req_queue.handle_req(req, wide);
        }
        else
        {
            this->wide_queue.handle_req(req, wide);
        }
        this->fsm_event.enqueue();
        return vp::IO_REQ_GRANTED;
    }
}

bool NetworkInterfaceV2::handle_request(FloonocNodeV2 *node, FloonocReqV2 *req, int from_x, int from_y)
{
    NetworkInterfaceV2 *origin_ni = req->src_ni;

    if (origin_ni == NULL)
    {
        // Response path: a reply has come back from the destination NI to us
        // (the source NI). Account it on the corresponding burst and reply to
        // the master when the burst is complete.
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received response from router (req: %p)\n", req);

        vp::IoReq *burst = req->burst;
        bool wide = req->wide;

        if (burst->get_is_write())
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received write burst response (burst: %p)\n",
                burst);
            this->nb_pending_bursts[wide]--;

            // Reply through the same external slave port the burst came in on.
            vp::IoSlave *port = (vp::IoSlave *)burst->initiator;
            burst->set_resp_status(vp::IO_RESP_OK);
            port->resp(burst);
        }
        else
        {
            this->trace.msg(vp::Trace::LEVEL_TRACE,
                "Reducing remaining size of burst (burst: %p, size: %d, req: %p, size %d)\n",
                burst, (int)burst->remaining_size, req, (int)req->get_size());
            burst->remaining_size -= req->get_size();

            if (burst->remaining_size == 0)
            {
                this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished burst (burst: %p)\n", burst);
                this->nb_pending_bursts[wide]--;
                vp::IoSlave *port = (vp::IoSlave *)burst->initiator;
                burst->set_resp_status(vp::IO_RESP_OK);
                port->resp(burst);
            }
        }

        this->fsm_event.enqueue();

        delete req;
    }
    else
    {
        // Request path: we are the destination NI. Forward to the local target
        // via our external master port.
        this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Received request from router (req: %p, base: 0x%x, size: 0x%x, isaddr: (%d), "
            "position: (%d, %d)) origin Ni: (%d, %d)\n",
            req, req->get_addr(), req->get_size(), (int)req->is_address, this->x,
            this->y, origin_ni->get_x(), origin_ni->get_y());

        if ((req->get_is_write() && !req->is_address) || !req->get_is_write())
        {
            bool is_stalled = false;

            bool wide = req->wide;
            vp::IoMaster *target = wide ? &this->wide_output_itf : &this->narrow_output_itf;
            this->trace.msg(vp::Trace::LEVEL_DEBUG,
                "Sending request to target (req: %p, base: 0x%x, size: 0x%x)\n",
                req, req->get_addr(), req->get_size());

            req->prepare();
            vp::IoReqStatus result = target->req(req);

            if (result == vp::IO_REQ_DONE)
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
                // v2 master holds the denied req and re-sends it from the
                // target's retry() callback (wide_retry / narrow_retry).
                if (wide)
                {
                    this->wide_target_stalled_req = req;
                    this->wide_routers_stalled = node;
                }
                else
                {
                    this->narrow_target_stalled_req = req;
                    this->narrow_routers_stalled = node;
                }
                this->target_stalled = true;
                is_stalled = true;
            }
            // GRANTED: async response will arrive via wide_response/narrow_response.

            return is_stalled;
        }
        else
        {
            // Address-only phase of a split write — no actual transfer to do.
            delete req;
        }
    }

    return false;
}


void NetworkInterfaceV2::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;

    _this->req_queue.check();
    _this->rsp_queue.check();
    _this->wide_queue.check();

    if (!_this->response_queue.empty())
    {
        _this->handle_response((FloonocReqV2 *)_this->response_queue.pop());
    }

    if (_this->routers_stalled && !_this->target_stalled)
    {
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

    // v2 deny/retry: if a master was previously denied and we now have capacity
    // on the corresponding external slave port, call retry() once. The master
    // will re-issue when it sees the retry callback.
    if (_this->owes_retry_wide_input &&
        _this->wide_read_pending_burst == NULL &&
        _this->wide_write_pending_burst == NULL &&
        _this->nb_pending_bursts[1] < _this->ni_outstanding_reqs)
    {
        _this->owes_retry_wide_input = false;
        _this->wide_input_itf.retry();
    }
    if (_this->owes_retry_narrow_input &&
        _this->narrow_read_pending_burst == NULL &&
        _this->narrow_write_pending_burst == NULL &&
        _this->nb_pending_bursts[0] < _this->ni_outstanding_reqs)
    {
        _this->owes_retry_narrow_input = false;
        _this->narrow_input_itf.retry();
    }

    _this->response_queue.trigger_next();
}


void NetworkInterfaceV2::handle_response(FloonocReqV2 *req)
{
    if (!req->get_is_write())
    {
        // Read response: send data back through the rsp/wide network to the
        // originating NI. handle_rsp allocates a fresh FloonocReqV2 for the
        // return trip, so the incoming req is safe to delete on the way out.
        NetworkInterfaceV2 *origin_ni = req->src_ni;

        req->dest_x = origin_ni->x;
        req->dest_y = origin_ni->y;
        if (req->wide)
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
        // Write response: only emit one ack through the mesh, after all
        // data-phase requests have arrived.
        vp::IoReq *burst = req->burst;

        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Reducing remaining size of burst (burst: %p, size: %d, req: %p, size %d)\n",
            burst, (int)burst->remaining_size, req, (int)req->get_size());
        burst->remaining_size -= req->get_size();

        if (burst->remaining_size == 0)
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished burst (burst: %p)\n", burst);
            NetworkInterfaceV2 *origin_ni = req->src_ni;

            req->dest_x = origin_ni->x;
            req->dest_y = origin_ni->y;
            this->rsp_queue.handle_rsp(req, true);
        }
    }
    delete req;
}
