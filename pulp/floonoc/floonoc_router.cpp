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



Router::Router(FlooNoc *noc, int x, int y, int queue_size)
    : vp::Block(noc, "router_" + std::to_string(x) + "_" + std::to_string(y)),
    fsm_event(this, &Router::fsm_handler)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->noc = noc;
    this->x = x;
    this->y = y;
    this->queue_size = queue_size;

    for (int i=0; i<5; i++)
    {
        this->input_queues[i] = new vp::Queue(this, "input_queue_" + std::to_string(i),
            &this->fsm_event);
    }
}



bool Router::handle_request(vp::IoReq *req, int from_x, int from_y)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handle request (req: %p, from: (%d, %d)\n", req, from_x, from_y);

    // Each direction has its own input queue to properly implement the round-robin
    // Get the one for the router or network interface which sent this request
    int queue_index = this->get_req_queue(from_x, from_y);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Pushed request to input queue (req: %p, queue: %d)\n", req, queue_index);

    // And push it to the queue. The queue will automatically trigger the FSM if needed
    vp::Queue *queue = this->input_queues[queue_index];
    queue->push_back(req);

    // We let the source enqueue one more request than what is possible to model the fact the fact
    // the request is stalled. This will then stall the source which will not send any request there
    // anymore until we unstall it
    return queue->size() > this->queue_size;
}



void Router::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Router *_this = (Router *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Checking pending requests\n");

    // Get the currently active queue and update it to implement the round-robin
    int queue_index = _this->current_queue;

    _this->current_queue += 1;
    if (_this->current_queue == 5)
    {
        _this->current_queue = 0;
    }

    // Then go through the 5 queues until we find a request which can be propagated
    for (int i=0; i<5; i++)
    {
        vp::Queue *queue = _this->input_queues[queue_index];
        // if (!queue->empty())
        if (queue->size())
        {
            vp::IoReq *req = (vp::IoReq *)queue->head();

            // Extract the destination from the request, that was filled in the network interface
            // when the request was created
            int to_x = req->get_int(FlooNoc::REQ_DEST_X);
            int to_y = req->get_int(FlooNoc::REQ_DEST_Y);

            // Get the next position in the grid. This takes care of deciding which path is taken
            // to go to the destination
            int next_x, next_y;
            _this->get_next_router_pos(to_x, to_y, next_x, next_y);
            uint8_t collective_type = _this->noc->collective? ((vp::IoReq *)(*req->arg_get(FlooNoc::REQ_DEST_BURST)))->get_payload()[0] : 0;
            bool is_collective_original_node = _this->noc->collective? (queue_index == _this->get_req_queue(_this->x, _this->y)) : 0;
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Resolved next position (req: %p, next_position: (%d, %d), coll: %d, coll_org: %d, dest_addr: 0x%lx)\n",
                req, next_x, next_y, collective_type, is_collective_original_node, req->get_addr());

            // In case the request goes to a queue which is stalled, skip it
            // we'll retry later
            int queue_id = _this->get_req_queue(next_x, next_y);
            if (_this->stalled_queues[queue_id])
            {
                continue;
            }

            // Since we now know that the request will be propagated, remove it from the queue
            queue->pop();
            if (queue->size() == _this->queue_size)
            {
                // In case the queue has one more element than possible, it means the output
                // queue of the sending router is stalled. Unstall it now that we can accept
                // one more request
                int pos_x, pos_y;
                // Get the previous position out of the input queue index
                _this->get_pos_from_queue(queue_id, pos_x, pos_y);

                if (pos_x == _this->x && pos_y == _this->y)
                {
                    // If the queue corresponds to the local one (previous position is same as
                    // position), it means it was injected by a network interface
                    NetworkInterface *ni = _this->noc->get_network_interface(_this->x, _this->y);
                    ni->unstall_queue(_this->x, _this->y);
                }
                else
                {
                    // Otherwise it comes from a router
                    Router *router = _this->noc->get_router(pos_x, pos_y);
                    router->unstall_queue(_this->x, _this->y);
                }
            }

            // Now send to the next position
            if (to_x == _this->x && to_y == _this->y)
            {
                if (collective_type > 1 && req->get_is_write())
                {
                    int from_x = req->get_int(FlooNoc::REQ_SRC_X);
                    int from_y = req->get_int(FlooNoc::REQ_SRC_Y);
                    uint8_t from_x_id = from_x - 1;
                    uint8_t from_y_id = from_y - 1;
                    uint8_t curr_x_id = _this->x - 1;
                    uint8_t curr_y_id = _this->y - 1;
                    uint8_t collective_row_mask = ((vp::IoReq *)(*req->arg_get(FlooNoc::REQ_DEST_BURST)))->get_payload()[1];
                    uint8_t collective_col_mask = ((vp::IoReq *)(*req->arg_get(FlooNoc::REQ_DEST_BURST)))->get_payload()[2];
                    bool check_x = (from_x_id & collective_row_mask) == (curr_x_id & collective_row_mask);
                    bool check_y = (from_y_id & collective_col_mask) == (curr_y_id & collective_col_mask);
                    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Collective Filter] From: (%d, %d), Row Mask: 0x%x, Col Mask: 0x%x, Check: (%d, %d)\n",
                        from_x_id, from_y_id, collective_row_mask, collective_col_mask, check_x, check_y);
                    //For reduction we do it on final target
                    if (check_x & check_y)
                    {
                        _this->handle_collective(req, collective_type, _this->x, _this->y);
                    }
                }
                // If next position is the same as the current one, it means it arrived to
                // destination, we need to forward to the fina target
                _this->send_to_target(req, _this->x, _this->y);
            }
            else
            {
                // Otherwise forward to next position
                Router *router = _this->noc->get_router(next_x, next_y);

                if (collective_type !=0 && is_collective_original_node == 0 && req->get_is_write())
                {
                    int from_x = req->get_int(FlooNoc::REQ_SRC_X);
                    int from_y = req->get_int(FlooNoc::REQ_SRC_Y);
                    uint8_t from_x_id = from_x - 1;
                    uint8_t from_y_id = from_y - 1;
                    uint8_t curr_x_id = _this->x - 1;
                    uint8_t curr_y_id = _this->y - 1;
                    uint8_t collective_row_mask = ((vp::IoReq *)(*req->arg_get(FlooNoc::REQ_DEST_BURST)))->get_payload()[1];
                    uint8_t collective_col_mask = ((vp::IoReq *)(*req->arg_get(FlooNoc::REQ_DEST_BURST)))->get_payload()[2];
                    bool check_x = (from_x_id & collective_row_mask) == (curr_x_id & collective_row_mask);
                    bool check_y = (from_y_id & collective_col_mask) == (curr_y_id & collective_col_mask);
                    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Collective Filter] From: (%d, %d), Row Mask: 0x%x, Col Mask: 0x%x, Check: (%d, %d)\n",
                        from_x_id, from_y_id, collective_row_mask, collective_col_mask, check_x, check_y);
                    if (check_x & check_y)
                    {
                        _this->handle_collective(req, collective_type, _this->x, _this->y);
                    }
                }

                if (router == NULL)
                {
                    // It is possible that we don't have any router at the destination if it is on
                    // the edge. In this case just forward to target
                    _this->send_to_target(req, next_x, next_y);
                }
                else
                {
                    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Forwarding request to next router (req: %p, next_position: (%d, %d))\n",
                        req, next_x, next_y);

                    // Send the request to next router, and in case it reports that its input queue
                    // is full, stall the corresponding output queue to make sure we stop sending
                    // there until the queue is unstalled
                    if (router->handle_request(req, _this->x, _this->y))
                    {
                        _this->stalled_queues[queue_id] = true;
                    }
                }
            }

            // Since we removed a request, check in next cycle if there is another one to handle
            _this->fsm_event.enqueue();

            // break;
        }

        // If we didn't any ready request, try with next queue
        queue_index += 1;
        if (queue_index == 5)
        {
            queue_index = 0;
        }
    }
}



void Router::send_to_target(vp::IoReq *req, int pos_x, int pos_y)
{
    vp::IoMaster *target = this->noc->get_target(pos_x, pos_y);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Sending request to target (req: %p, position: (%d, %d))\n",
        req, pos_x, pos_y);

    vp::IoReqStatus result = target->req(req);
    if (result == vp::IO_REQ_OK || result == vp::IO_REQ_INVALID)
    {
        // If the request is processed synchronously, immediately notify the network interface

        // We need to store the status in the request so that it is properly propagated to the
        // initiator request
        req->status = result;
        this->noc->handle_request_end(req);
    }
    else if (result == vp::IO_REQ_DENIED)
    {
        int queue = this->get_req_queue(pos_x, pos_y);

        // In case it is denied, the request has been queued in the target, we just need to make
        // sure we don't send any other request there until we reveive the grant callback
        this->stalled_queues[queue] = true;

        // Store the router in the request. Since the grant is received by top noc,
        // it will use this argument to notify the router about the grant
        *(Router **)req->arg_get(FlooNoc::REQ_ROUTER) = this;
        // Also store the queue, the router will use it to know which queue to unstall
        *(int *)req->arg_get(FlooNoc::REQ_QUEUE) = queue;
    }
    else
    {
        // In case of asynchronous response, the network interface will be notified by the
        // the response callback
    }
}


/****************************************************
*                   FP16 Utilities                  *
****************************************************/

typedef union {
    float f;
    struct {
        uint32_t mantissa : 23;
        uint32_t exponent : 8;
        uint32_t sign : 1;
    } parts;
} FloatBits;

typedef uint16_t fp16;

// Convert float to FP16 (half-precision)
fp16 float_to_fp16(float value) {
    FloatBits floatBits;
    floatBits.f = value;

    uint16_t sign = floatBits.parts.sign << 15;
    int32_t exponent = floatBits.parts.exponent - 127 + 15; // adjust bias from 127 to 15
    uint32_t mantissa = floatBits.parts.mantissa >> 13;     // reduce to 10 bits

    if (exponent <= 0) {
        if (exponent < -10) return sign;   // too small
        mantissa = (floatBits.parts.mantissa | 0x800000) >> (1 - exponent);
        return sign | mantissa;
    } else if (exponent >= 0x1F) {
        return sign | 0x7C00;  // overflow to infinity
    }
    return sign | (exponent << 10) | mantissa;
}

// Convert FP16 to float
float fp16_to_float(fp16 value) {
    FloatBits floatBits;
    floatBits.parts.sign = (value >> 15) & 0x1;
    int32_t exponent = (value >> 10) & 0x1F;
    floatBits.parts.exponent = (exponent == 0) ? 0 : exponent + 127 - 15;
    floatBits.parts.mantissa = (value & 0x3FF) << 13;
    return floatBits.f;
}

void Router::handle_collective(vp::IoReq *req, uint8_t collective_type, int pos_x, int pos_y)
{
    vp::IoMaster *target = this->noc->get_target(pos_x, pos_y);

    if (collective_type == 1)
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "[BroadCast] handle collective operation to target (target addr: 0x%lx, target size: 0x%lx, position: (%d, %d))\n",
        req->get_addr(), req->get_size(), pos_x, pos_y);

        //Generate a broadcst write request
        vp::IoReq *collective_req = new vp::IoReq(req->get_addr(), req->get_data(), req->get_size(), 1);
        //Send to target
        vp::IoReqStatus result = target->req(collective_req);
        if (result != vp::IO_REQ_OK)
        {
            this->trace.fatal("Invalid collective operation response from TCDM: %d\n", result);
        }
        //delete the broadcst write request
        delete collective_req;
    } else if (collective_type == 2){
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction ADD UINT16] handle collective operation to target (target addr: 0x%lx, target size: 0x%lx, position: (%d, %d))\n",
        req->get_addr(), req->get_size(), pos_x, pos_y);

        //Generate a broadcst write request
        uint8_t* tmp_array = new uint8_t[req->get_size()];
        vp::IoReq *collective_req = new vp::IoReq(req->get_addr(), tmp_array, req->get_size(), 0);
        //Send to target
        vp::IoReqStatus result = target->req(collective_req);
        if (result != vp::IO_REQ_OK)
        {
            this->trace.fatal("Invalid collective operation response from TCDM: %d\n", result);
        }
        //Execute reduction
        uint16_t * dst = (uint16_t *) req->get_data();
        uint16_t * src = (uint16_t *) collective_req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(uint16_t)); ++i)
        {
            dst[i] = dst[i] + src[i];
        }
        //delete the broadcst write request
        delete collective_req;
        delete[] tmp_array;
    } else if (collective_type == 3){
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction ADD INT16] handle collective operation to target (target addr: 0x%lx, target size: 0x%lx, position: (%d, %d))\n",
        req->get_addr(), req->get_size(), pos_x, pos_y);

        //Generate a broadcst write request
        uint8_t* tmp_array = new uint8_t[req->get_size()];
        vp::IoReq *collective_req = new vp::IoReq(req->get_addr(), tmp_array, req->get_size(), 0);
        //Send to target
        vp::IoReqStatus result = target->req(collective_req);
        if (result != vp::IO_REQ_OK)
        {
            this->trace.fatal("Invalid collective operation response from TCDM: %d\n", result);
        }
        //Execute reduction
        int16_t * dst = (int16_t *) req->get_data();
        int16_t * src = (int16_t *) collective_req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(int16_t)); ++i)
        {
            dst[i] = dst[i] + src[i];
        }
        //delete the broadcst write request
        delete collective_req;
        delete[] tmp_array;
    } else if (collective_type == 4){
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction ADD FP16] handle collective operation to target (target addr: 0x%lx, target size: 0x%lx, position: (%d, %d))\n",
        req->get_addr(), req->get_size(), pos_x, pos_y);

        //Generate a broadcst write request
        uint8_t* tmp_array = new uint8_t[req->get_size()];
        vp::IoReq *collective_req = new vp::IoReq(req->get_addr(), tmp_array, req->get_size(), 0);
        //Send to target
        vp::IoReqStatus result = target->req(collective_req);
        if (result != vp::IO_REQ_OK)
        {
            this->trace.fatal("Invalid collective operation response from TCDM: %d\n", result);
        }
        //Execute reduction
        fp16 * dst = (fp16 *) req->get_data();
        fp16 * src = (fp16 *) collective_req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(fp16)); ++i)
        {
            dst[i] = float_to_fp16(fp16_to_float(dst[i]) + fp16_to_float(src[i]));
        }
        //delete the broadcst write request
        delete collective_req;
        delete[] tmp_array;
    } else if (collective_type == 5){
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction MAX UINT16] handle collective operation to target (target addr: 0x%lx, target size: 0x%lx, position: (%d, %d))\n",
        req->get_addr(), req->get_size(), pos_x, pos_y);

        //Generate a broadcst write request
        uint8_t* tmp_array = new uint8_t[req->get_size()];
        vp::IoReq *collective_req = new vp::IoReq(req->get_addr(), tmp_array, req->get_size(), 0);
        //Send to target
        vp::IoReqStatus result = target->req(collective_req);
        if (result != vp::IO_REQ_OK)
        {
            this->trace.fatal("Invalid collective operation response from TCDM: %d\n", result);
        }
        //Execute reduction
        uint16_t * dst = (uint16_t *) req->get_data();
        uint16_t * src = (uint16_t *) collective_req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(uint16_t)); ++i)
        {
            dst[i] = dst[i] > src[i] ? dst[i] : src[i];
        }
        //delete the broadcst write request
        delete collective_req;
        delete[] tmp_array;
    } else if (collective_type == 6){
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction MAX INT16] handle collective operation to target (target addr: 0x%lx, target size: 0x%lx, position: (%d, %d))\n",
        req->get_addr(), req->get_size(), pos_x, pos_y);

        //Generate a broadcst write request
        uint8_t* tmp_array = new uint8_t[req->get_size()];
        vp::IoReq *collective_req = new vp::IoReq(req->get_addr(), tmp_array, req->get_size(), 0);
        //Send to target
        vp::IoReqStatus result = target->req(collective_req);
        if (result != vp::IO_REQ_OK)
        {
            this->trace.fatal("Invalid collective operation response from TCDM: %d\n", result);
        }
        //Execute reduction
        int16_t * dst = (int16_t *) req->get_data();
        int16_t * src = (int16_t *) collective_req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(int16_t)); ++i)
        {
            dst[i] = dst[i] > src[i] ? dst[i] : src[i];
        }
        //delete the broadcst write request
        delete collective_req;
        delete[] tmp_array;
    } else if (collective_type == 7){
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction MAX FP16] handle collective operation to target (target addr: 0x%lx, target size: 0x%lx, position: (%d, %d))\n",
        req->get_addr(), req->get_size(), pos_x, pos_y);

        //Generate a broadcst write request
        uint8_t* tmp_array = new uint8_t[req->get_size()];
        vp::IoReq *collective_req = new vp::IoReq(req->get_addr(), tmp_array, req->get_size(), 0);
        //Send to target
        vp::IoReqStatus result = target->req(collective_req);
        if (result != vp::IO_REQ_OK)
        {
            this->trace.fatal("Invalid collective operation response from TCDM: %d\n", result);
        }
        //Execute reduction
        fp16 * dst = (fp16 *) req->get_data();
        fp16 * src = (fp16 *) collective_req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(fp16)); ++i)
        {
            dst[i] = float_to_fp16(fp16_to_float(dst[i]) > fp16_to_float(src[i]) ? fp16_to_float(dst[i]) : fp16_to_float(src[i]));
        }
        //delete the broadcst write request
        delete collective_req;
        delete[] tmp_array;
    } else {
        this->trace.fatal("Invalid collective operation: %d\n", collective_type);
    }
}


void Router::grant(vp::IoReq *req)
{
    // Now that the stalled request has been granted, we need to unstall the queue
    int queue = *(int *)req->arg_get(FlooNoc::REQ_QUEUE);
    this->stalled_queues[queue] = false;

    // And check in next cycle if another request can be sent
    this->fsm_event.enqueue(1);
}



void Router::get_next_router_pos(int dest_x, int dest_y, int &next_x, int &next_y)
{
    // XY routing algorithm
    int eff_dest_x = dest_x;

    if (dest_x == 0 && dest_y != this->y)
    {
        eff_dest_x = 1;
    } else
    if (dest_x == (this->noc->dim_x + 1) && dest_y != this->y)
    {
        eff_dest_x = this->noc->dim_x;
    }

    if (eff_dest_x == this->x && dest_y == this->y)
    {
        next_x = this->x;
        next_y = this->y;
    }
    else if (eff_dest_x == this->x)
    {
        next_x = this->x;
        next_y = dest_y < this->y ? this->y - 1 : this->y + 1;
    }
    else
    {
        next_x = eff_dest_x < this->x ? this->x - 1 : this->x + 1;
        next_y = this->y;
    }

}



void Router::unstall_queue(int from_x, int from_y)
{
    // This gets called when an output queue gets unstalled because the denied request gets granted.
    // Just unstall the queue and trigger the fsm, in case we can now send a new request
    int queue = this->get_req_queue(from_x, from_y);
    this->stalled_queues[queue] = false;
    this->fsm_event.enqueue();
}



void Router::get_pos_from_queue(int queue, int &pos_x, int &pos_y)
{
    switch (queue)
    {
        case FlooNoc::DIR_RIGHT: pos_x = this->x+1; pos_y = this->y; break;
        case FlooNoc::DIR_LEFT: pos_x = this->x-1; pos_y = this->y; break;
        case FlooNoc::DIR_UP: pos_x = this->x; pos_y = this->y+1; break;
        case FlooNoc::DIR_DOWN: pos_x = this->x; pos_y = this->y-1; break;
        case FlooNoc::DIR_LOCAL: pos_x = this->x; pos_y = this->y; break;
    }
}



int Router::get_req_queue(int from_x, int from_y)
{
    int queue_index = 0;
    if (from_x != this->x)
    {
        queue_index = from_x < this->x ? FlooNoc::DIR_RIGHT : FlooNoc::DIR_LEFT;
    }
    else if (from_y != this->y)
    {
        queue_index = from_y < this->y ? FlooNoc::DIR_UP : FlooNoc::DIR_DOWN;
    }
    else
    {
        queue_index = FlooNoc::DIR_LOCAL;
    }

    return queue_index;
}



void Router::reset(bool active)
{
    if (active)
    {
        this->current_queue = 0;
    }
}

