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

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "floonoc_v2.hpp"
#include "floonoc_router_v2.hpp"
#include "floonoc_network_interface_v2.hpp"

RouterV2::RouterV2(FlooNocV2 *noc, std::string name, int x, int y, int queue_size)
    : FloonocNodeV2(noc, name + std::to_string(x) + "_" + std::to_string(y)),
      fsm_event(this, &RouterV2::fsm_handler),
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

    for (int i = 0; i < 5; i++)
    {
        this->input_queues[i] = new RouterQueueV2(this, "input_queue_" + std::to_string(i),
            &this->fsm_event);

        this->stalled_queues[i] = false;
    }
}

RouterV2::~RouterV2()
{
    for (int i = 0; i < 5; i++)
    {
        delete this->input_queues[i];
    }
}

void RouterV2::set_neighbour(int dir, FloonocNodeV2 *node)
{
    this->output_nodes[dir] = node;
}

bool RouterV2::handle_request(FloonocNodeV2 *node, FloonocReqV2 *req, int from_x, int from_y)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handle request (req: %p, base: 0x%x, size: 0x%x, from: (%d, %d)\n", req, req->get_addr(), req->get_size(), from_x, from_y);

    this->signal_req.set_and_release(req->initiator_addr);
    this->signal_req_size.set_and_release(req->get_size());
    this->signal_req_is_write.set_and_release(req->get_is_write());

    int queue_index = this->get_req_queue(from_x, from_y);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Pushed request to input queue (req: %p, queue: %d)\n", req, queue_index);

    RouterQueueV2 *queue = this->input_queues[queue_index];
    queue->queue.push_back(req, 1);

    bool stalled = queue->queue.size() > this->queue_size;
    if (stalled)
    {
        queue->stalled_node = node;
    }
    return stalled;
}

void RouterV2::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    RouterV2 *_this = (RouterV2 *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Checking pending requests\n");
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Current queue: %d\n", _this->current_queue);
    int in_queue_index = _this->current_queue;

    bool output_full[5] = {false};
    for (int i = 0; i < 5; i++)
    {
        RouterQueueV2 *queue = _this->input_queues[in_queue_index];
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Checking input queue (queue_index: %d, queue size: %d)\n", in_queue_index, queue->queue.size());
        if (!queue->queue.empty())
        {
            FloonocReqV2 *req = (FloonocReqV2 *)queue->queue.head();

            int to_x = req->dest_x;
            int to_y = req->dest_y;

            int next_x, next_y;
            _this->get_next_router_pos(to_x, to_y, next_x, next_y);
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Resolved next position (req: %p, dest: (%d, %d), next_position: (%d, %d))\n",
                             req, to_x, to_y, next_x, next_y);

            int out_queue_id = _this->get_req_queue(next_x, next_y);

            if (output_full[out_queue_id])
            {
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Output queue is full, skipping (out queue: %d)\n", out_queue_id);
                _this->fsm_event.enqueue();
                in_queue_index += 1;
                if (in_queue_index == 5)
                {
                    in_queue_index = 0;
                }
                continue;
            }
            output_full[out_queue_id] = true;

            if (_this->stalled_queues[out_queue_id])
            {
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Output queue is stalled, skipping (out queue: %d)\n", out_queue_id);
                in_queue_index += 1;
                if (in_queue_index == 5)
                {
                    in_queue_index = 0;
                }
                continue;
            }

            queue->queue.pop();

            if (queue->queue.size() == _this->queue_size)
            {
                queue->stalled_node->unstall_queue(_this->x, _this->y);
            }

            FloonocNodeV2 *node = _this->output_nodes[out_queue_id];

            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Forwarding request to next router (req: %p, base: 0x%x, size: 0x%x, next_position: (%d, %d), in_queue: %d)\n",
                                req, req->get_addr(), req->get_size(), next_x, next_y, in_queue_index);
            if (node->handle_request(_this, req, _this->x, _this->y))
            {
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Stalling queue (position: (%d, %d), queue: %d)\n", _this->x, _this->y, out_queue_id);
                _this->stalled_queues[out_queue_id] = true;
            }
            _this->current_queue = in_queue_index + 1;
            if (_this->current_queue == 5)
            {
                _this->current_queue = 0;
            }

            _this->fsm_event.enqueue();
        }
        else
        {
            if (queue->queue.size())
            {
               _this->fsm_event.enqueue();
            }
        }

        in_queue_index += 1;
        if (in_queue_index == 5)
        {
            in_queue_index = 0;
        }
    }
}

void RouterV2::get_next_router_pos(int dest_x, int dest_y, int &next_x, int &next_y)
{
    if (dest_x < 0)
    {
        switch (dest_x + 4)
        {
            case FlooNocV2::DIR_UP: next_x = this->x; next_y = this->y + 1; break;
            case FlooNocV2::DIR_DOWN: next_x = this->x; next_y = this->y - 1; break;
            case FlooNocV2::DIR_RIGHT: next_y = this->y; next_x = this->x + 1; break;
            case FlooNocV2::DIR_LEFT: next_y = this->y; next_x = this->x - 1; break;
        }
    }
    else
    {
        if (dest_x == this->x && dest_y == this->y)
        {
            next_x = this->x;
            next_y = this->y;
            return;
        }

        if (dest_x != this->x)
        {
            next_x = dest_x < this->x ? this->x - 1 : this->x + 1;
            next_y = this->y;

            if (next_x != 0 && next_x != this->noc->dim_x - 1 || next_y == dest_y)
            {
                return;
            }
        }

        next_x = this->x;
        next_y = dest_y < this->y ? this->y - 1 : this->y + 1;
    }
}

void RouterV2::unstall_queue(int from_x, int from_y)
{
    int queue = this->get_req_queue(from_x, from_y);
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (position: (%d, %d), queue: %d)\n", from_x, from_y, queue);
    this->stalled_queues[queue] = false;
    this->fsm_event.enqueue();
}

void RouterV2::stall_queue(int from_x, int from_y)
{
    int queue = this->get_req_queue(from_x, from_y);
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Stalling queue (position: (%d, %d), queue: %d)\n", from_x, from_y, queue);
    this->stalled_queues[queue] = true;
}

void RouterV2::get_pos_from_queue(int queue, int &pos_x, int &pos_y)
{
    switch (queue)
    {
    case FlooNocV2::DIR_RIGHT:
        pos_x = this->x + 1;
        pos_y = this->y;
        break;
    case FlooNocV2::DIR_LEFT:
        pos_x = this->x - 1;
        pos_y = this->y;
        break;
    case FlooNocV2::DIR_UP:
        pos_x = this->x;
        pos_y = this->y + 1;
        break;
    case FlooNocV2::DIR_DOWN:
        pos_x = this->x;
        pos_y = this->y - 1;
        break;
    case FlooNocV2::DIR_LOCAL:
        pos_x = this->x;
        pos_y = this->y;
        break;
    }
}

int RouterV2::get_req_queue(int from_x, int from_y)
{
    int queue_index = 0;
    if (from_x != this->x)
    {
        queue_index = from_x < this->x ? FlooNocV2::DIR_LEFT : FlooNocV2::DIR_RIGHT;
    }
    else if (from_y != this->y)
    {
        queue_index = from_y < this->y ? FlooNocV2::DIR_DOWN : FlooNocV2::DIR_UP;
    }
    else
    {
        queue_index = FlooNocV2::DIR_LOCAL;
    }

    return queue_index;
}

void RouterV2::reset(bool active)
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

RouterQueueV2::RouterQueueV2(vp::Block *parent, std::string name, vp::ClockEvent *ready_event)
: queue(parent, name, ready_event)
{
}
