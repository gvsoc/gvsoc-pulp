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
      signal_narrow_req(*this, "narrow_req", 64)
{
    this->noc = noc;
    this->x = x;
    this->y = y;
    this->target = noc->get_target(x, y);

    traces.new_trace("trace", &trace, vp::DEBUG);

    // Network interface input port
    this->narrow_input_itf.set_req_meth(&NetworkInterface::narrow_req);
    noc->new_slave_port("in_" + std::to_string(x) + "_" + std::to_string(y),
                        &this->narrow_input_itf, this);

}

void NetworkInterface::reset(bool active)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Resetting network interface\n");
    if (active)
    {
        this->router_stalled = false;
        this->target_stalled = false;
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
    // The request which was previously denied has been granted. Unstall the output queue.
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (position: (%d, %d), queue: %d)\n", from_x, from_y);
    this->router_stalled = false;
}

vp::IoReqStatus NetworkInterface::narrow_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    _this->signal_narrow_req = req->get_addr();
    vp::IoReqStatus result = _this->req(_this, req);
    return result;
}

// Called by the local initiator to inject a request into the noc.
vp::IoReqStatus NetworkInterface::req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request from initiator (req: %p)\n", req);

    if (_this->router_stalled)
    {
        return vp::IO_REQ_DENIED;
    }

    _this->handle_req(req);
    return vp::IO_REQ_PENDING;
}

void NetworkInterface::handle_req(vp::IoReq *req)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handling request (req: %p)\n", req);

    // Fill in the information needed by the target network interface to send back the response
    *req->arg_get(FlooNoc::REQ_SRC_NI) = (void *)this;

    // The router may refuse the request if its input queue is full.
    // In this case we must stall the network interface.
    Router *router = this->noc->get_req_router(this->x, this->y);

    this->router_stalled = router->handle_request(req, this->x, this->y);
    if (this->router_stalled)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Stalling network interface (position: (%d, %d))\n", this->x, this->y);
    }
}


void NetworkInterface::req_from_router(vp::IoReq *req, int from_x, int from_y)
{
    NetworkInterface *origin_ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_SRC_NI);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request from router(req: %p, position: (%d, %d)) origin Ni: (%d, %d)\n",
                    req, this->x, this->y, origin_ni->get_x(), origin_ni->get_y());

    vp::IoMaster *target = this->target;
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Sending request to target (target name: %s)(req: %p, base: 0x%x, size: 0x%x, position: (%d, %d))\n",
                    target->get_name().c_str(), req, req->get_addr(), req->get_size(), this->x, this->y);
    // This does the actual operation(read, write or atomic operation) on the target
    // Note: Memory is read/written already here. The backward path is only used to get the delay of the network.
    vp::IoReqStatus result = target->req(req);

    if (result == vp::IO_REQ_DENIED)
    {
        // Store the NI in the request. Since the grant is received by top noc,
        // it will use this argument to notify the NI about the grant
        *(NetworkInterface **)req->arg_get(FlooNoc::REQ_NI) = this;
        this->target_stalled = true;

        // In case the access is denied, we need to stall all routers going this NI to prevent
        // any other request from arriving.
        this->noc->get_req_router(from_x, from_y)->stall_queue(this->x, this->y);
    }
}


void NetworkInterface::grant(vp::IoReq *req)
{
    this->target_stalled = false;
    this->noc->get_req_router(this->x, this->y)->unstall_queue(this->x, this->y);
}
