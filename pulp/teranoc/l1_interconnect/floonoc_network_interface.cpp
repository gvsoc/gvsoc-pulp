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
      fsm_event(this, &NetworkInterface::fsm_handler), signal_narrow_req(*this, "narrow_req", 64)
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
        this->stalled = false;
        this->pending_burst_size = 0;
        this->denied_req = NULL;
        this->target_stalled = false;
        this->routers_stalled = false;
        // while (this->pending_bursts.size() > 0)
        // {
        //     this->remove_pending_burst();
        // }
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
    vp::IoReqStatus result = _this->req(_this, req);
    return result;
}

// This, respectively the narrow and wide versions, should be called by the cluster (or initator of the axi burst)
vp::IoReqStatus NetworkInterface::req(vp::Block *__this, vp::IoReq *req)
{
    // This gets called when a burst is received
    NetworkInterface *_this = (NetworkInterface *)__this;

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request from initiator (req: %p)\n", req);

    // We also need to push at which timestamp the burst can start being processed.
    // Since we handle it asynchronously, we need to start it only once its latency has been
    // reached
    // req->set_latency(0); // Actually dont do that because the cluster sent them with some latency that doesnt make sense

    if (_this->stalled)
    {
        _this->fsm_event.enqueue();

        if (_this->denied_req)
        {
            _this->trace.fatal("A request is already pending in the network interface while it is stalled (req: %p, denied_req: %p)\n",
                             req, _this->denied_req);
            return vp::IO_REQ_DENIED;
        }
        _this->denied_req = req;
        return vp::IO_REQ_DENIED;
    }
    else
    {
        _this->handle_req(req);
        return vp::IO_REQ_PENDING;
    }
}

void NetworkInterface::handle_req(vp::IoReq *req)
{
    // Get the current burst to be processed
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handling request (req: %p)\n", req);

    // Fill in the information needed by the target network interface to send back the response
    *req->arg_get(FlooNoc::REQ_SRC_NI) = (void *)this;

    // int to_x, to_y;
    // to_x = (int)*req->arg_get(FlooNoc::REQ_DEST_X);
    // to_y = (int)*req->arg_get(FlooNoc::REQ_DEST_Y);

    // this->trace.msg(vp::Trace::LEVEL_TRACE, "Sending request to router (req: %p, destination: (%d, %d))\n",
    //                 req, to_x, to_y);

    // Note that the router may not grant the request if its input queue is full.
    // In this case we must stall the network interface
    Router *router = this->noc->get_req_router(this->x, this->y);

    this->stalled = router->handle_request(req, this->x, this->y);
    if (this->stalled)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Stalling network interface (position: (%d, %d))\n", this->x, this->y);
    }

    // Since we processed a burst, we need to check again in the next cycle if there is
    // anything to do.
    this->fsm_event.enqueue();
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
        this->routers_stalled = true;
    }
}


void NetworkInterface::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    NetworkInterface *_this = (NetworkInterface *)__this;
    if (_this->denied_req && !_this->stalled)
    {
        vp::IoReq *req = _this->denied_req;
        _this->denied_req = NULL;
        _this->handle_req(req);
        req->get_resp_port()->grant(req);
    }
}


void NetworkInterface::grant(vp::IoReq *req)
{
    this->target_stalled = false;
    if (this->routers_stalled)
    {
        this->noc->get_req_router(this->x, this->y)->unstall_queue(this->x, this->y);
        this->routers_stalled = false;
    }
    this->fsm_event.enqueue();
}
