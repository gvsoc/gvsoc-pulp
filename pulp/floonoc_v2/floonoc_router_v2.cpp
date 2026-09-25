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
#include "floonoc_v2.hpp"
#include "floonoc_router_v2.hpp"

static const char *dir_names[RouterV2::DIR_NB] = {"right", "left", "up", "down", "local"};

RouterV2::RouterV2(vp::ComponentConf &config)
    : vp::Component(config),
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
      }},
      input_ports{{
        FloonocLinkSlave(DIR_RIGHT, &RouterV2::link_req),
        FloonocLinkSlave(DIR_LEFT, &RouterV2::link_req),
        FloonocLinkSlave(DIR_UP, &RouterV2::link_req),
        FloonocLinkSlave(DIR_DOWN, &RouterV2::link_req),
        FloonocLinkSlave(DIR_LOCAL, &RouterV2::link_req)
      }},
      output_ports{{
        FloonocLinkMaster(DIR_RIGHT, &RouterV2::link_unstall),
        FloonocLinkMaster(DIR_LEFT, &RouterV2::link_unstall),
        FloonocLinkMaster(DIR_UP, &RouterV2::link_unstall),
        FloonocLinkMaster(DIR_DOWN, &RouterV2::link_unstall),
        FloonocLinkMaster(DIR_LOCAL, &RouterV2::link_unstall)
      }}
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->x = get_js_config()->get_int("x");
    this->y = get_js_config()->get_int("y");
    this->dim_x = get_js_config()->get_int("dim_x");
    this->dim_y = get_js_config()->get_int("dim_y");
    this->queue_size = get_js_config()->get_int("router_input_queue_size");

    for (int i = 0; i < DIR_NB; i++)
    {
        this->input_queues[i] = new vp::Queue(this, "input_queue_" + std::to_string(i),
            &this->fsm_event);

        this->new_slave_port(std::string("input_") + dir_names[i], &this->input_ports[i]);
        this->new_master_port(std::string("output_") + dir_names[i], &this->output_ports[i]);

        this->stalled_queues[i] = false;
    }
}

RouterV2::~RouterV2()
{
    for (int i = 0; i < DIR_NB; i++)
    {
        delete this->input_queues[i];
    }
}

bool RouterV2::link_req(vp::Block *__this, FloonocReqV2 *req, int queue_index)
{
    RouterV2 *_this = (RouterV2 *)__this;

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handle request (req: %p, base: 0x%lx, size: 0x%lx, queue: %d)\n",
        req, req->get_addr(), req->get_size(), queue_index);

    _this->signal_req.set_and_release(req->initiator_addr);
    _this->signal_req_size.set_and_release(req->get_size());
    _this->signal_req_is_write.set_and_release(req->get_is_write());

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Pushed request to input queue (req: %p, queue: %d)\n",
        req, queue_index);

    vp::Queue *queue = _this->input_queues[queue_index];
    queue->push_back(req, 1);

    return queue->size() > _this->queue_size;
}

void RouterV2::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    RouterV2 *_this = (RouterV2 *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Checking pending requests\n");
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Current queue: %d\n", _this->current_queue);
    int in_queue_index = _this->current_queue;

    bool output_full[DIR_NB] = {false};
    for (int i = 0; i < DIR_NB; i++)
    {
        vp::Queue *queue = _this->input_queues[in_queue_index];
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Checking input queue (queue_index: %d, queue size: %d)\n", in_queue_index, queue->size());
        if (!queue->empty())
        {
            FloonocReqV2 *req = (FloonocReqV2 *)queue->head();

            int to_x = req->dest_x;
            int to_y = req->dest_y;

            int next_x, next_y;
            _this->get_next_router_pos(to_x, to_y, next_x, next_y);
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Resolved next position (req: %p, dest: (%d, %d), next_position: (%d, %d))\n",
                             req, to_x, to_y, next_x, next_y);

            int out_queue_id = _this->get_req_queue(next_x, next_y);

            // Wormhole: an output mid-packet is reserved for its owning input.
            // A flit from any other input must wait until the owner passes its
            // tail flit and releases the output.
            if (_this->output_owner[out_queue_id] != -1 &&
                _this->output_owner[out_queue_id] != in_queue_index)
            {
                int owner = _this->output_owner[out_queue_id];
                _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                    "Output locked to another input, skipping (out queue: %d, owner: %d)\n",
                    out_queue_id, owner);
                in_queue_index += 1;
                if (in_queue_index == DIR_NB)
                {
                    in_queue_index = 0;
                }
                continue;
            }

            if (output_full[out_queue_id])
            {
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Output queue is full, skipping (out queue: %d)\n", out_queue_id);
                _this->fsm_event.enqueue();
                in_queue_index += 1;
                if (in_queue_index == DIR_NB)
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
                if (in_queue_index == DIR_NB)
                {
                    in_queue_index = 0;
                }
                continue;
            }

            queue->pop();

            if (queue->size() == _this->queue_size)
            {
                _this->input_ports[in_queue_index].unstall();
            }

            // Wormhole: lock the output to this input for the rest of the
            // packet; release it once the tail (is_last) flit is forwarded. A
            // single-flit packet (is_first && is_last) leaves it free.
            _this->output_owner[out_queue_id] = req->is_last ? -1 : in_queue_index;

            _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                "NOC_V2_HOP req=%p from=(%d,%d) to=(%d,%d)\n",
                req, _this->x, _this->y, next_x, next_y);
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Forwarding request to next router (req: %p, base: 0x%lx, size: 0x%lx, next_position: (%d, %d), in_queue: %d)\n",
                                req, req->get_addr(), req->get_size(), next_x, next_y, in_queue_index);
            if (_this->output_ports[out_queue_id].req(req))
            {
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Stalling queue (position: (%d, %d), queue: %d)\n", _this->x, _this->y, out_queue_id);
                _this->stalled_queues[out_queue_id] = true;
            }
            _this->current_queue = in_queue_index + 1;
            if (_this->current_queue == DIR_NB)
            {
                _this->current_queue = 0;
            }

            _this->fsm_event.enqueue();
        }
        else
        {
            if (queue->size())
            {
               _this->fsm_event.enqueue();
            }
        }

        in_queue_index += 1;
        if (in_queue_index == DIR_NB)
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
            case DIR_UP: next_x = this->x; next_y = this->y + 1; break;
            case DIR_DOWN: next_x = this->x; next_y = this->y - 1; break;
            case DIR_RIGHT: next_y = this->y; next_x = this->x + 1; break;
            case DIR_LEFT: next_y = this->y; next_x = this->x - 1; break;
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

            if (next_x != 0 && next_x != this->dim_x - 1 || next_y == dest_y)
            {
                return;
            }
        }

        next_x = this->x;
        next_y = dest_y < this->y ? this->y - 1 : this->y + 1;
    }
}

void RouterV2::link_unstall(vp::Block *__this, int output_id)
{
    RouterV2 *_this = (RouterV2 *)__this;
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (queue: %d)\n", output_id);
    _this->stalled_queues[output_id] = false;
    _this->fsm_event.enqueue();
}

int RouterV2::get_req_queue(int from_x, int from_y)
{
    int queue_index = 0;
    if (from_x != this->x)
    {
        queue_index = from_x < this->x ? DIR_LEFT : DIR_RIGHT;
    }
    else if (from_y != this->y)
    {
        queue_index = from_y < this->y ? DIR_DOWN : DIR_UP;
    }
    else
    {
        queue_index = DIR_LOCAL;
    }

    return queue_index;
}

void RouterV2::reset(bool active)
{
    if (active)
    {
        this->current_queue = 0;
        for (int i = 0; i < DIR_NB; i++)
        {
            this->stalled_queues[i] = false;
            this->output_owner[i] = -1;
        }
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new RouterV2(config);
}
