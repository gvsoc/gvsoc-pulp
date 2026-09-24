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

#include <string.h>
#include <vp/vp.hpp>
#include "floonoc_v2.hpp"
#include "floonoc_router_v2.hpp"

RouterV2::RouterV2(vp::ComponentConf &config)
    : vp::Component(config, this->cfg),
      fsm_event(this, &RouterV2::fsm_handler),
      signal_req(*this, "req", 64, vp::SignalCommon::ResetKind::HighZ),
      signal_req_size(*this, "req_size", 64, vp::SignalCommon::ResetKind::HighZ),
      signal_req_is_write(*this, "req_is_write", 1, vp::SignalCommon::ResetKind::HighZ)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    if (strcmp(this->cfg.route_algo, "xy") == 0)
    {
        this->xy_routing = true;
    }
    else if (strcmp(this->cfg.route_algo, "id_table") == 0)
    {
        this->xy_routing = false;
    }
    else
    {
        this->trace.fatal("Unknown routing algorithm: %s\n", this->cfg.route_algo);
    }

    this->nb_ports = this->cfg.ports_count;
    if (this->xy_routing && this->nb_ports != DIR_NB)
    {
        this->trace.fatal("XY routing needs %d ports, got %d\n", DIR_NB, this->nb_ports);
    }

    for (int i = 0; i < this->nb_ports; i++)
    {
        const FloonocRouterPortV2 &port = this->cfg.ports[i];

        this->input_stages.push_back(port.stages);
        this->queue_capacity.push_back(this->cfg.queue_size + port.stages);

        this->input_queues.push_back(new vp::Queue(this, "input_queue_" + std::to_string(i),
            &this->fsm_event));

        this->input_ports.emplace_back(new FloonocLinkSlave(i, &RouterV2::link_req));
        this->output_ports.emplace_back(new FloonocLinkMaster(i, &RouterV2::link_unstall));
        this->new_slave_port(std::string("input_") + port.name, this->input_ports[i].get());
        this->new_master_port(std::string("output_") + port.name, this->output_ports[i].get());

        this->stalled_queues.emplace_back(new vp::Signal<bool>(*this,
            std::string("stalled_queue_") + port.name, 1));

        this->output_owner.push_back(-1);
    }

    for (size_t i = 0; i < this->cfg.routes_count; i++)
    {
        const FloonocRouteV2 &route = this->cfg.routes[i];
        if (route.dest < 0 || route.port < 0 || route.port >= this->nb_ports)
        {
            this->trace.fatal("Invalid route (dest: %d, port: %d)\n", (int)route.dest,
                (int)route.port);
        }
        if (route.dest >= (int64_t)this->routing_table.size())
        {
            this->routing_table.resize(route.dest + 1, -1);
        }
        this->routing_table[route.dest] = route.port;
    }
}

RouterV2::~RouterV2()
{
    for (vp::Queue *queue : this->input_queues)
    {
        delete queue;
    }
}

bool RouterV2::link_req(vp::Block *__this, FloonocReqV2 *req, int queue_index)
{
    RouterV2 *_this = (RouterV2 *)__this;

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Handle request (req: %p, base: 0x%x, size: 0x%x, queue: %d)\n",
        req, req->get_addr(), req->get_size(), queue_index);

    _this->signal_req.set_and_release(req->initiator_addr);
    _this->signal_req_size.set_and_release(req->get_size());
    _this->signal_req_is_write.set_and_release(req->get_is_write());

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Pushed request to input queue (req: %p, queue: %d)\n",
        req, queue_index);

    // One cycle through the router input, plus the link pipeline stages.
    vp::Queue *queue = _this->input_queues[queue_index];
    queue->push_back(req, 1 + _this->input_stages[queue_index]);

    return queue->size() > _this->queue_capacity[queue_index];
}

void RouterV2::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    RouterV2 *_this = (RouterV2 *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Checking pending requests\n");
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Current queue: %d\n", _this->current_queue);
    int in_queue_index = _this->current_queue;
    int nb_ports = _this->nb_ports;

    std::vector<bool> output_full(nb_ports, false);
    for (int i = 0; i < nb_ports; i++)
    {
        vp::Queue *queue = _this->input_queues[in_queue_index];
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Checking input queue (queue_index: %d, queue size: %d)\n", in_queue_index, queue->size());
        if (!queue->empty())
        {
            FloonocReqV2 *req = (FloonocReqV2 *)queue->head();

            int out_queue_id = _this->get_output_port(req->dest_id);
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Resolved output port (req: %p, dest: %d, port: %d)\n",
                             req, req->dest_id, out_queue_id);

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
                if (in_queue_index == nb_ports)
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
                if (in_queue_index == nb_ports)
                {
                    in_queue_index = 0;
                }
                continue;
            }
            output_full[out_queue_id] = true;

            if (*_this->stalled_queues[out_queue_id])
            {
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Output queue is stalled, skipping (out queue: %d)\n", out_queue_id);
                in_queue_index += 1;
                if (in_queue_index == nb_ports)
                {
                    in_queue_index = 0;
                }
                continue;
            }

            queue->pop();

            if (queue->size() == _this->queue_capacity[in_queue_index])
            {
                _this->input_ports[in_queue_index]->unstall();
            }

            // Wormhole: lock the output to this input for the rest of the
            // packet; release it once the tail (is_last) flit is forwarded. A
            // single-flit packet (is_first && is_last) leaves it free.
            _this->output_owner[out_queue_id] = req->is_last ? -1 : in_queue_index;

            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Forwarding request to next router (req: %p, base: 0x%x, size: 0x%x, port: %d, in_queue: %d)\n",
                                req, req->get_addr(), req->get_size(), out_queue_id, in_queue_index);
            if (_this->output_ports[out_queue_id]->req(req))
            {
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Stalling queue (queue: %d)\n", out_queue_id);
                *_this->stalled_queues[out_queue_id] = true;
            }
            _this->current_queue = in_queue_index + 1;
            if (_this->current_queue == nb_ports)
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
        if (in_queue_index == nb_ports)
        {
            in_queue_index = 0;
        }
    }
}

int RouterV2::get_output_port(int dest_id)
{
    if (this->xy_routing)
    {
        int next_x, next_y;
        this->get_next_router_pos(dest_id, next_x, next_y);
        return this->get_req_queue(next_x, next_y);
    }

    if (dest_id < 0 || dest_id >= (int)this->routing_table.size() ||
        this->routing_table[dest_id] == -1)
    {
        this->trace.fatal("No route to node %d\n", dest_id);
    }
    return this->routing_table[dest_id];
}

void RouterV2::get_next_router_pos(int dest_id, int &next_x, int &next_y)
{
    int x = this->cfg.x;
    int y = this->cfg.y;

    if (dest_id < 0)
    {
        // Target reached by leaving the mesh in one direction
        // (FlooNocV2Direction, -4..-1).
        switch (dest_id + 4)
        {
            case DIR_UP: next_x = x; next_y = y + 1; break;
            case DIR_DOWN: next_x = x; next_y = y - 1; break;
            case DIR_RIGHT: next_y = y; next_x = x + 1; break;
            case DIR_LEFT: next_y = y; next_x = x - 1; break;
        }
    }
    else
    {
        int dest_x = floonoc_xy_node_x(dest_id);
        int dest_y = floonoc_xy_node_y(dest_id);

        if (dest_x == x && dest_y == y)
        {
            next_x = x;
            next_y = y;
            return;
        }

        if (dest_x != x)
        {
            next_x = dest_x < x ? x - 1 : x + 1;
            next_y = y;

            if (next_x != 0 && next_x != this->cfg.dim_x - 1 || next_y == dest_y)
            {
                return;
            }
        }

        next_x = x;
        next_y = dest_y < y ? y - 1 : y + 1;
    }
}

void RouterV2::link_unstall(vp::Block *__this, int output_id)
{
    RouterV2 *_this = (RouterV2 *)__this;
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (queue: %d)\n", output_id);
    *_this->stalled_queues[output_id] = false;
    _this->fsm_event.enqueue();
}

int RouterV2::get_req_queue(int from_x, int from_y)
{
    int x = this->cfg.x;
    int y = this->cfg.y;
    int queue_index = 0;
    if (from_x != x)
    {
        queue_index = from_x < x ? DIR_LEFT : DIR_RIGHT;
    }
    else if (from_y != y)
    {
        queue_index = from_y < y ? DIR_DOWN : DIR_UP;
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
        for (int i = 0; i < this->nb_ports; i++)
        {
            *this->stalled_queues[i] = false;
            this->output_owner[i] = -1;
        }
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new RouterV2(config);
}
