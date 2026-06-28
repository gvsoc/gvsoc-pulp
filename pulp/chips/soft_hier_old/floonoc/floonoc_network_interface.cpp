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

    // Network interface input port
    this->input_itf.set_req_meth(&NetworkInterface::req);
    noc->new_slave_port("input_" + std::to_string(x) + "_"  + std::to_string(y),
        &this->input_itf, this);

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
    if (active)
    {
        this->stalled = false;
        this->pending_burst_size = 0;
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
    // - There is at least one available internal request
    if (!_this->stalled && _this->pending_bursts.size() > 0 && _this->free_reqs.size() > 0)
    {
        vp::IoReq *burst = _this->pending_bursts.front();

        // In case the burst is being handled for the first time, initialize the current burst
        if (_this->pending_burst_size == 0)
        {
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Start handling burst (burst: %p, base: 0x%llx, size: 0x%x, is_write: %d)\n",
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
        }

        // THen pop an internal request and fill it from current burst information
        vp::IoReq *req = _this->free_reqs.front();
        _this->free_reqs.pop();

        // Get base from current burst
        uint64_t base = _this->pending_burst_base;

        // We apply the HBM node aliasing
        if (_this->noc->edge_node_alias > 1)
        {
            Entry *test_entry = _this->noc->get_entry(base, 1);
            if (test_entry == NULL) _this->trace.fatal("[NoC Edge Alias] Cannot find entry for base 0x%llx \n",base);
            if (test_entry->x == 0)
            {
                //west edge
                uint64_t node_id = _this->y - 1;
                uint64_t alias_offset = 1;
                alias_offset = alias_offset << _this->noc->edge_node_alias_start_bit;
                base += alias_offset * (node_id % _this->noc->edge_node_alias);
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[NoC Edge Alias] West edge node %d alias to 0x%llx \n", node_id, base);
            } else if (test_entry->x == _this->noc->dim_x - 1)
            {
                //east edge
                uint64_t node_id = _this->y - 1;
                uint64_t alias_offset = 1;
                alias_offset = alias_offset << _this->noc->edge_node_alias_start_bit;
                base += alias_offset * (node_id % _this->noc->edge_node_alias);
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[NoC Edge Alias] East edge node %d alias to 0x%llx \n", node_id, base);
            } else if (test_entry->y == 0)
            {
                //south edge
                uint64_t node_id = _this->x - 1;
                uint64_t alias_offset = 1;
                alias_offset = alias_offset << _this->noc->edge_node_alias_start_bit;
                base += alias_offset * (node_id % _this->noc->edge_node_alias);
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[NoC Edge Alias] South edge node %d alias to 0x%llx \n", node_id, base);
            } else if (test_entry->y == _this->noc->dim_y - 1)
            {
                //north edge
                uint64_t node_id = _this->x - 1;
                uint64_t alias_offset = 1;
                alias_offset = alias_offset << _this->noc->edge_node_alias_start_bit;
                base += alias_offset * (node_id % _this->noc->edge_node_alias);
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[NoC Edge Alias] North edge node %d alias to 0x%llx \n", node_id, base);
            }

        }

        //If support interleaving
        if (_this->noc->interleave_enable && base >= _this->noc->interleave_region_base && base < _this->noc->interleave_region_base + _this->noc->interleave_region_size)
        {
            uint32_t mask = ((1 << _this->noc->interleave_bit_width) - 1);
            uint32_t range1 = (base >> _this->noc->interleave_granularity) & mask;
            uint32_t range2 = (base >> _this->noc->interleave_bit_start) & mask;
            base &= ~(mask << _this->noc->interleave_granularity);
            base &= ~(mask << _this->noc->interleave_bit_start);
            base |= (range1 << _this->noc->interleave_bit_start);
            base |= (range2 << _this->noc->interleave_granularity);
        }

        // Size must be at max the noc width to respect the bandwidth
        uint64_t size = std::min(_this->noc->width, _this->pending_burst_size);
        // And must not cross a page to fall into one target
        uint64_t next_page = (base + _this->noc->width - 1) & ~(_this->noc->width - 1);
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
        req->set_size(size);
        req->set_data(_this->pending_burst_data);
        req->set_is_write(burst->get_is_write());
        if (_this->noc->atomics)
        {
            req->set_opcode(burst->get_opcode());
            req->set_second_data(burst->get_second_data());
        }

        //Deal with collective primitives
        *req->arg_get(FlooNoc::REQ_PARENT) = (void *)0;
        *req->arg_get(FlooNoc::REQ_COLL_TYPE) = (void *)0;
        *req->arg_get(FlooNoc::REQ_ROW_MASK) = (void *)0;
        *req->arg_get(FlooNoc::REQ_COL_MASK) = (void *)0;
        *req->arg_get(FlooNoc::REQ_PEND_KIDS) = (void *)0;
        *req->arg_get(FlooNoc::REQ_MOMENTUM) = (void *)FlooNoc::MOMENTUM_ZERO;
        if (_this->noc->collective)
        {
            uint8_t collective_type = burst->get_payload()[0];
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Collective] preload[0] of burst %d\n",collective_type);
            if (collective_type>0 && collective_type<8)
            {
                uint8_t row_mask = burst->get_payload()[1];
                uint8_t col_mask = burst->get_payload()[2];
                req->set_int(FlooNoc::REQ_COLL_TYPE, collective_type);
                req->set_int(FlooNoc::REQ_ROW_MASK, row_mask);
                req->set_int(FlooNoc::REQ_COL_MASK, col_mask);
            }
        }

        // Get the target entry corresponding to the current base
        Entry *entry = _this->noc->get_entry(base, size);
        if (entry == NULL)
        {
            // If any request of the burst is invalid because no target was found, make the whole
            // burst invalid.
            _this->trace.force_warning("No entry found for burst (base: 0x%llx, size: 0x%x)",
                base, size);
            burst->status = vp::IO_REQ_INVALID;

            // Stop the burst
            *(int *)burst->arg_get_last() -= _this->pending_burst_size;
            _this->pending_burst_size = 0;
            _this->pending_bursts.pop();
            // And respond to the current burst as it is over with an error only if there is
            // no request on-going for it.
            // Otherwise, the response will be sent when last request response is received
            if (*(int *)burst->arg_get_last() == 0)
            {
                burst->get_resp_port()->resp(burst);
            }
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
            }

            // Store information in the request which will be needed by the routers and the target
            req->set_addr(base - entry->base);
            *req->arg_get(FlooNoc::REQ_DEST_X) = (void *)(long)entry->x;
            *req->arg_get(FlooNoc::REQ_DEST_Y) = (void *)(long)entry->y;
            *req->arg_get(FlooNoc::REQ_SRC_X) = (void *)(long)_this->x;
            *req->arg_get(FlooNoc::REQ_SRC_Y) = (void *)(long)_this->y;

            // And forward to the first router which is at the same position as the network
            // interface
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Injecting request to noc (req: %p, base: 0x%llx, size: 0x%x, op_code: %0d, destination: (%d, %d))\n",
                req, base, size, req->get_opcode(), entry->x, entry->y);

            // Noe that the router may not grant tje request if its input queue is full.
            // In this case we must stall the network interface
            Router *router = _this->noc->get_router(_this->x, _this->y);
            _this->stalled = router->handle_request(req, _this->x, _this->y);
        }

        // Since we processed a burst, we need to check again in the next cycle if there is
        // anything to do.
        _this->fsm_event.enqueue();
    }
}



void NetworkInterface::handle_response(vp::IoReq *req)
{
    // This gets called by the routers when an internal request has been handled
    // First extract the corresponding burst from the request so that we can update the burst.
    vp::IoReq *burst = *(vp::IoReq **)req->arg_get(1);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request response (req: %p)\n", req);

    // If at least one of the request is invalid, this makes the whole burst invalid
    if (req->status == vp::IO_REQ_INVALID)
    {
        burst->status = vp::IO_REQ_INVALID;
    }

    // Account the received response on the burst
    *(int *)burst->arg_get_last() -= req->get_size();

    // And respond to it if all responses have been received
    if (*(int *)burst->arg_get_last() == 0)
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished burst (burst: %p)\n", burst);
        burst->get_resp_port()->resp(burst);
    }

    // The request is now available
    this->free_reqs.push(req);
    // Trigger the FSM since something may need to be done now that a new request is available
    this->fsm_event.enqueue();
}



vp::IoReqStatus NetworkInterface::req(vp::Block *__this, vp::IoReq *req)
{
    // This gets called when a burst is received
    NetworkInterface *_this = (NetworkInterface *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received burst (burst: %p, offset: 0x%llx, size: 0x%x, is_write: %d, op: %d)\n",
        req, offset, size, req->get_is_write(), req->get_opcode());

    // Just enqueue it and trigger the FSM which will check if it must be processed now
    _this->pending_bursts.push(req);
    _this->fsm_event.enqueue();

    return vp::IO_REQ_PENDING;
}
