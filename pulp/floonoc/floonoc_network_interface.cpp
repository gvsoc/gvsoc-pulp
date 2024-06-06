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
    fsm_event(this, &NetworkInterface::fsm_handler)
{
    this->noc = noc;
    this->x = x;
    this->y = y;

    traces.new_trace("trace", &trace, vp::DEBUG);

    this->input_itf.set_req_meth(&NetworkInterface::req);
    noc->new_slave_port("input_" + std::to_string(x) + "_"  + std::to_string(y),
        &this->input_itf, this);

    int ni_outstanding_reqs = this->noc->get_js_config()->get("ni_outstanding_reqs")->get_int();

    for (int i=0; i<ni_outstanding_reqs; i++)
    {
        this->free_reqs.push(new vp::IoReq());
    }
}


void NetworkInterface::reset(bool active)
{
    if (active)
    {
        this->stalled = false;
        this->pending_burst_size = 0;
    }
}



void NetworkInterface::unstall_queue(int from_x, int from_y)
{

}



void NetworkInterface::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    NetworkInterface *_this = (NetworkInterface *)__this;

    if (_this->pending_bursts.size() > 0 && _this->free_reqs.size() > 0)
    {
        vp::IoReq *burst = _this->pending_bursts.front();

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
            *(int *)burst->arg_get_last() = burst->get_size();
        }

        vp::IoReq *req = _this->free_reqs.front();
        _this->free_reqs.pop();

        int size = std::min(_this->noc->width, _this->pending_burst_size);

        uint64_t base = _this->pending_burst_base;

        req->init();
        req->arg_alloc(FlooNoc::REQ_NB_ARGS);
        *req->arg_get(FlooNoc::REQ_DEST_NI) = (void *)_this;
        *req->arg_get(FlooNoc::REQ_DEST_BURST) = (void *)burst;
        *req->arg_get(FlooNoc::REQ_DEST_BASE) = (void *)base;
        req->set_size(size);
        req->set_data(_this->pending_burst_data);
        req->set_is_write(burst->get_is_write());

        _this->pending_burst_base += size;
        _this->pending_burst_data += size;
        _this->pending_burst_size -= size;

        if (_this->pending_burst_size == 0)
        {
            _this->pending_bursts.pop();
        }

        Entry *entry = NULL;

        for (int i=0; i<_this->noc->entries.size(); i++)
        {
            entry = &_this->noc->entries[i];
            if (base >= entry->base && base + size <= entry->base + entry->size)
            {
                break;
            }
        }

        if (entry)
        {
            req->set_addr(base - entry->base);
            *req->arg_get(FlooNoc::REQ_DEST_X) = (void *)(long)entry->x;
            *req->arg_get(FlooNoc::REQ_DEST_Y) = (void *)(long)entry->y;
        }


        Router *router = _this->noc->get_router(_this->x, _this->y);

        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Injecting request to noc (req: %p, base: 0x%x, size: 0x%x, destination: (%d, %d))\n",
            req, base, size, entry->x, entry->y);

        _this->stalled = router->handle_request(req, _this->x, _this->y);

        _this->check_state();
    }
}

void NetworkInterface::handle_response(vp::IoReq *req)
{
    vp::IoReq *burst = *(vp::IoReq **)req->arg_get(1);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request response (req: %p)\n", req);

    // If at least one of the request is invalid, this makes the whole burst invalid
    if (req->status == vp::IO_REQ_INVALID)
    {
        burst->status = vp::IO_REQ_INVALID;
    }

    *(int *)burst->arg_get_last() -= req->get_size();

    if (*(int *)burst->arg_get_last() == 0)
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished burst (burst: %p)\n", burst);
        burst->get_resp_port()->resp(burst);
    }

    this->free_reqs.push(req);

    this->check_state();
}

vp::IoReqStatus NetworkInterface::req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterface *_this = (NetworkInterface *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received burst (burst: %p, offset: 0x%x, size: 0x%x, is_write: %d, op: %d)\n",
        req, offset, size, req->get_is_write(), req->get_opcode());

    _this->pending_bursts.push(req);

    _this->check_state();

    return vp::IO_REQ_PENDING;
}



void NetworkInterface::check_state()
{
    if (!this->fsm_event.is_enqueued())
    {
        if (!this->stalled && this->pending_bursts.size() > 0)
        {
            this->fsm_event.enqueue(1);
        }
    }
}
