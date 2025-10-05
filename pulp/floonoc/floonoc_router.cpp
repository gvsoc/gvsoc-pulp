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
 *          Jonas Martin, ETH (martinjo@student.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "floonoc.hpp"
#include "floonoc_router.hpp"
#include "floonoc_network_interface.hpp"

Router::Router(FlooNoc *noc, std::string name, int x, int y, int queue_size)
    : FloonocNode(noc, name + std::to_string(x) + "_" + std::to_string(y)),
      fsm_event(this, &Router::fsm_handler),
      signal_req(*this, "req", 64, vp::SignalCommon::ResetKind::HighZ),
      signal_req_size(*this, "req_size", 64, vp::SignalCommon::ResetKind::HighZ),
      signal_req_is_write(*this, "req_is_write", 1, vp::SignalCommon::ResetKind::HighZ),
      stalled_queues{{
        vp::Signal<bool>(*this, "stalled_queue_right", 1),
        vp::Signal<bool>(*this, "stalled_queue_left", 1),
        vp::Signal<bool>(*this, "stalled_queue_up", 1),
        vp::Signal<bool>(*this, "stalled_queue_down", 1),
        vp::Signal<bool>(*this, "stalled_queue_local", 1)
      }}
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->noc = noc;
    this->x = x;
    this->y = y;
    this->queue_size = queue_size;

    // Create a queue for each direction (N, E, S, W, local)
    for (int i = 0; i < 5; i++)
    {
        this->input_queues[i] = new RouterQueue(this, "input_queue_" + std::to_string(i),
            &this->fsm_event);

        this->stalled_queues[i] = false;
    }
}

Router::~Router()
{
    for (int i = 0; i < 5; i++)
    {
        delete this->input_queues[i];
    }
}

void Router::set_neighbour(int dir, FloonocNode *node)
{
    this->output_nodes[dir] = node;
}

bool Router::handle_request(FloonocNode *node, vp::IoReq *req, int from_x, int from_y)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handle request (req: %p, base: 0x%x, size: 0x%x, from: (%d, %d)\n", req, req->get_addr(), req->get_size(), from_x, from_y);

    this->signal_req.set_and_release(req->get_addr());
    this->signal_req_size.set_and_release(req->get_size());
    this->signal_req_is_write.set_and_release(req->get_is_write());

    // Each direction has its own input queue to properly implement the round-robin
    // Get the one for the router or network interface which sent this request
    int queue_index = this->get_req_queue(from_x, from_y);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Pushed request to input queue (req: %p, queue: %d)\n", req, queue_index);

    // And push it to the queue. The queue will automatically trigger the FSM if needed
    RouterQueue *queue = this->input_queues[queue_index];
    queue->queue.push_back(req, 1); // The queue has an intrinsic delay of 1. With this additional delay, we model the fact that a real router takes 2 cycles to forward a request

    // We let the source enqueue one more request than what is possible to model the fact
    // the request is stalled. This will then stall the source which will not send any request there
    // anymore until we unstall it
    bool stalled = queue->queue.size() > this->queue_size;
    if (stalled)
    {
        queue->stalled_node = node;
    }
    return stalled;
}

void Router::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Router *_this = (Router *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Checking pending requests\n");
    // The routers can process 1 incoming request from each direction and send 1 request to each of the directions in 1 cycle
    // The round robin is used to make sure we don't always process the same direction first
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Current queue: %d\n", _this->current_queue);
    // Get the currently active queue and update it to implement the round-robin
    int in_queue_index = _this->current_queue;

    // Update the current queue so that another one is checked first during the next cycle
    // _this->current_queue += 1;
    // if (_this->current_queue == 5)
    // {
    //     _this->current_queue = 0;
    // }
    bool output_full[5] = {false}; // Used to make sure we only send a single request per cycle to each direction
    // Then go through the 5 input queues until we find a request which can be propagated
    for (int i = 0; i < 5; i++)
    {
        RouterQueue *queue = _this->input_queues[in_queue_index];
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Checking input queue (queue_index: %d, queue size: %d)\n", in_queue_index, queue->queue.size());
        if (!queue->queue.empty())
        {
            vp::IoReq *req = (vp::IoReq *)queue->queue.head();

            // Extract the destination from the request, that was filled in the network interface
            // when the request was created
            int to_x = req->get_int(FlooNoc::REQ_DEST_X);
            int to_y = req->get_int(FlooNoc::REQ_DEST_Y);

            // Get the next position in the grid. This takes care of deciding which path is taken
            // to go to the destination
            int next_x, next_y;
            _this->get_next_router_pos(to_x, to_y, next_x, next_y);
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Resolved next position (req: %p, dest: (%d, %d), next_position: (%d, %d))\n",
                             req, to_x, to_y, next_x, next_y);

            // Get output queue ID from next position
            int out_queue_id = _this->get_req_queue(next_x, next_y);

            // Only send one request per cycle to the same output
            if (output_full[out_queue_id])
            {
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Output queue is full, skipping (out queue: %d)\n", out_queue_id);
                _this->fsm_event.enqueue(); // Check again in next cycle
                in_queue_index += 1;
                if (in_queue_index == 5)
                {
                    in_queue_index = 0;
                }
                continue; // Skip this request and isntead check another input queue
            }
            output_full[out_queue_id] = true;

            // In case the request goes to a queue which is stalled, skip it
            // we'll retry later
            if (_this->stalled_queues[out_queue_id])
            {
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Output queue is stalled, skipping (out queue: %d)\n", out_queue_id);
                // Don't enque here because the stalled router will notifiy once it is unstalled
                in_queue_index += 1;
                if (in_queue_index == 5)
                {
                    in_queue_index = 0;
                }
                continue;
            }

            // Since we now know, that the request will be propagated, remove it from the queue
            queue->queue.pop();

            if (queue->queue.size() == _this->queue_size) // Remember we let the source enqueue one more request than what is possible.
            {
                // In case the queue had one more element than possible, it means the output
                // queue of the sending router is stalled. Unstall it now that we can accept
                // one more request
                queue->stalled_node->unstall_queue(_this->x, _this->y);
            }

            // Otherwise forward to next position
            FloonocNode *node = _this->output_nodes[out_queue_id];

            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Forwarding request to next router (req: %p, base: 0x%x, size: 0x%x, next_position: (%d, %d), in_queue: %d)\n",
                                req, req->get_addr(), req->get_size(), next_x, next_y, in_queue_index);
            // Send the request to next router, and in case it reports that its input queue
            // is full, stall the corresponding output queue to make sure we stop sending
            // there until the queue is unstalled
            if (node->handle_request(_this, req, _this->x, _this->y))
            {
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Stalling queue (position: (%d, %d), queue: %d)\n", _this->x, _this->y, out_queue_id);
                _this->stalled_queues[out_queue_id] = true;
            }
            _this->current_queue = in_queue_index + 1; // Always start looking from the queue after the one that has been processed last
            if (_this->current_queue == 5)
            {
                _this->current_queue = 0;
            }

            // Since we removed a request, check in next cycle if there is another one to handle
            _this->fsm_event.enqueue();
        }
        else
        {
            if (queue->queue.size())
            {
               _this->fsm_event.enqueue();
            }
        }

        // Go to next input queue
        in_queue_index += 1;
        if (in_queue_index == 5)
        {
            in_queue_index = 0;
        }
    }
}

// This is determining the routes the requests will take in the network
void Router::get_next_router_pos(int dest_x, int dest_y, int &next_x, int &next_y)
{
    if (dest_x < 0)
    {
        switch (dest_x + 4)
        {
            case FlooNoc::DIR_UP: next_x = this->x; next_y = this->y + 1; break;
            case FlooNoc::DIR_DOWN: next_x = this->x; next_y = this->y - 1; break;
            case FlooNoc::DIR_RIGHT: next_y = this->y; next_x = this->x + 1; break;
            case FlooNoc::DIR_LEFT: next_y = this->y; next_x = this->x - 1; break;
        }
    }
    else
    {
        // TODO If there is a gap in the mesh of routers this algorithm doesnt work
        if (dest_x == this->x && dest_y == this->y)
        {
            next_x = this->x;
            next_y = this->y;
        }
        else if (dest_x == this->x)
        {
            next_x = this->x;
            next_y = dest_y < this->y ? this->y - 1 : this->y + 1;
        }
        else
        {
            next_x = dest_x < this->x ? this->x - 1 : this->x + 1;
            next_y = this->y;
        }
    }
}

void Router::unstall_queue(int from_x, int from_y)
{
    // This gets called when an output queue gets unstalled because the denied request gets granted.
    // Just unstall the queue and trigger the fsm, in case we can now send a new request
    int queue = this->get_req_queue(from_x, from_y);
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (position: (%d, %d), queue: %d)\n", from_x, from_y, queue);
    this->stalled_queues[queue] = false;
    // And check in next cycle if another request can be sent
    this->fsm_event.enqueue();
}

void Router::stall_queue(int from_x, int from_y)
{
    int queue = this->get_req_queue(from_x, from_y);
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Stalling queue (position: (%d, %d), queue: %d)\n", from_x, from_y, queue);
    this->stalled_queues[queue] = true;
}

void Router::get_pos_from_queue(int queue, int &pos_x, int &pos_y)
{
    switch (queue)
    {
    case FlooNoc::DIR_RIGHT:
        pos_x = this->x + 1;
        pos_y = this->y;
        break;
    case FlooNoc::DIR_LEFT:
        pos_x = this->x - 1;
        pos_y = this->y;
        break;
    case FlooNoc::DIR_UP:
        pos_x = this->x;
        pos_y = this->y + 1;
        break;
    case FlooNoc::DIR_DOWN:
        pos_x = this->x;
        pos_y = this->y - 1;
        break;
    case FlooNoc::DIR_LOCAL:
        pos_x = this->x;
        pos_y = this->y;
        break;
    }
}

int Router::get_req_queue(int from_x, int from_y)
{
    int queue_index = 0;
    if (from_x != this->x)
    {
        queue_index = from_x < this->x ? FlooNoc::DIR_LEFT : FlooNoc::DIR_RIGHT;
    }
    else if (from_y != this->y)
    {
        queue_index = from_y < this->y ? FlooNoc::DIR_DOWN : FlooNoc::DIR_UP;
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
        for (int i = 0; i < 5; i++)
        {
            this->stalled_queues[i] = false;
        }
    }

}

RouterQueue::RouterQueue(vp::Block *parent, std::string name, vp::ClockEvent *ready_event)
: queue(parent, name, ready_event)
{

}
