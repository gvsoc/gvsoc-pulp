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
}

bool Router::handle_request(vp::IoReq *req, int from_x, int from_y)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handle request (req: %p, from: (%d, %d)\n", req, from_x, from_y);

    int queue_index = this->get_req_queue(from_x, from_y);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Pushed request to input queue (req: %p, queue: %d)\n", req, queue_index);

    std::queue<vp::IoReq *> &queue = this->input_queues[queue_index];

    queue.push(req);

    // Since we pushed a new request, check in next cycle if it needs to be handled
    this->fsm_event.enqueue(1);

    return queue.size() > this->queue_size;
}

void Router::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Router *_this = (Router *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Checking pending requests\n");

    int queue_index = _this->current_queue;

    _this->current_queue += 1;
    if (_this->current_queue == 5)
    {
        _this->current_queue = 0;
    }

    for (int i=0; i<5; i++)
    {
        std::queue<vp::IoReq *> &queue = _this->input_queues[queue_index];
        if (!queue.empty())
        {
            vp::IoReq *req = queue.front();

            int to_x = req->get_int(FlooNoc::REQ_DEST_X);
            int to_y = req->get_int(FlooNoc::REQ_DEST_Y);

            // In case the request goes to a queue which is stalled, skip it
            // we'll retry later
            int queue_id = _this->get_req_queue(to_x, to_y);
            if (_this->stalled_queues[queue_id])
            {
                continue;
            }

            queue.pop();
            if (queue.size() == _this->queue_size)
            {
                // In case the queue has one more element than possible, it means the output
                // queue of the sending router is stalled. Unstall it now that we can accept
                // one more request
                int pos_x, pos_y;
                _this->get_pos_from_queue(queue_id, pos_x, pos_y);
                Router *router = _this->noc->get_router(pos_x, pos_y);
                if (router != NULL)
                {
                    router->unstall_queue(_this->x, _this->y);
                }
                else
                {
                    NetworkInterface *ni = _this->noc->get_network_interface(_this->x, _this->y);
                    ni->unstall_queue(_this->x, _this->y);
                }
            }

            if (to_x == _this->x && to_y == _this->y)
            {
                _this->send_to_target(req, _this->x, _this->y);
            }
            else
            {
                int next_x, next_y;
                _this->get_next_router_pos(to_x, to_y, next_x, next_y);

                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Resolved next position (req: %p, next_position: (%d, %d))\n",
                    req, next_x, next_y);

                Router *router = _this->noc->get_router(next_x, next_y);

                if (router == NULL)
                {
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
            _this->fsm_event.enqueue(1);

            break;
        }

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
    else if (vp::IO_REQ_DENIED)
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
    int x_diff = dest_x - this->x;
    int y_diff = dest_y - this->y;

    if (std::abs(x_diff) > std::abs(y_diff))
    {
        next_x = x_diff < 0 ? this->x - 1 : this->x + 1;
        next_y = this->y;
    }
    else
    {
        next_y = y_diff < 0 ? this->y - 1 : this->y + 1;
        next_x = this->x;
    }
}



void Router::unstall_queue(int from_x, int from_y)
{
    int queue = this->get_req_queue(from_x, from_y);
    this->stalled_queues[queue] = false;
    this->fsm_event.enqueue(1);
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

