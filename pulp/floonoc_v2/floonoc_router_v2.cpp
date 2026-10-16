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

#include <algorithm>
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
        this->output_queues.push_back(new vp::Queue(this, "output_queue_" + std::to_string(i),
            &this->fsm_event));

        this->input_ports.emplace_back(new FloonocLinkSlave(i, &RouterV2::link_req));
        this->output_ports.emplace_back(new FloonocLinkMaster(i, &RouterV2::link_unstall));
        this->new_slave_port(std::string("input_") + port.name, this->input_ports[i].get());
        this->new_master_port(std::string("output_") + port.name, this->output_ports[i].get());

        this->stalled_queues.emplace_back(new vp::Signal<bool>(*this,
            std::string("stalled_queue_") + port.name, 1));

        this->unstall_cycle.push_back(-1);
        this->arbiters.push_back(OutputArbiter());
    }

    this->scratch_output_full.resize(this->nb_ports);
    this->scratch_head_output.resize(this->nb_ports);
    this->scratch_valid_in.resize(this->nb_ports);

    // The arbiters keep their request vectors as 32-bit masks
    if (this->nb_ports > 32)
    {
        this->trace.fatal("At most 32 ports, got %d\n", this->nb_ports);
    }
    while ((1 << this->arb_levels) < this->nb_ports)
    {
        this->arb_levels++;
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
    for (vp::Queue *queue : this->output_queues)
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
    queue->push_back(req, _this->input_stages[queue_index]);

    return queue->size() >= _this->queue_capacity[queue_index];
}

void RouterV2::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    RouterV2 *_this = (RouterV2 *)__this;
    int nb_ports = _this->nb_ports;
    bool pending = false;
    int64_t cycle = _this->clock.get_cycles();

    // Output FIFOs full at the start of the cycle can't take a flit in it
    std::vector<bool> &output_full = _this->scratch_output_full;
    for (int out = 0; out < nb_ports; out++)
    {
        output_full[out] = _this->output_queues[out]->size() >= OUTPUT_DEPTH;
    }

    // Output FIFOs: their head flit goes on the link, unless the next node
    // is full.
    for (int out = 0; out < nb_ports; out++)
    {
        vp::Queue *queue = _this->output_queues[out];
        if (*_this->stalled_queues[out] || queue->empty() || _this->unstall_cycle[out] == cycle)
        {
            continue;
        }
        FloonocReqV2 *req = (FloonocReqV2 *)queue->pop();
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Forwarding request (req: %p, base: 0x%x, size: 0x%x, port: %d)\n",
            req, req->get_addr(), req->get_size(), out);
        if (_this->output_ports[out]->req(req))
        {
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Stalling output (port: %d)\n", out);
            *_this->stalled_queues[out] = true;
        }
    }

    // Output of the head flit of each input, -1 if none.
    std::vector<int> &head_output = _this->scratch_head_output;
    for (int in = 0; in < nb_ports; in++)
    {
        vp::Queue *queue = _this->input_queues[in];
        head_output[in] = queue->empty() ? -1 :
            _this->get_output_port(((FloonocReqV2 *)queue->head())->dest_id);
    }

    // One arbiter per output, as the RTL floo_wormhole_arbiter: it arbitrates
    // the inputs valid at the last tail flit (valid_q), an rr_arb_tree with
    // FairArb and LockIn, and lets the selected input through if it is valid
    // and the output FIFO has room. A flit wins only its own output.
    std::vector<uint32_t> &valid_in = _this->scratch_valid_in;
    std::fill(valid_in.begin(), valid_in.end(), 0);
    for (int in = 0; in < nb_ports; in++)
    {
        if (head_output[in] != -1)
        {
            valid_in[head_output[in]] |= 1 << in;
        }
    }

    for (int out = 0; out < nb_ports; out++)
    {
        OutputArbiter &arb = _this->arbiters[out];
        uint32_t valid_i = valid_in[out];
        if (valid_i == 0 && arb.valid_q == 0 && !arb.lock_q && !arb.last_q)
        {
            // Idle, the update would leave the state as it is
            continue;
        }
        uint32_t valid_d = (arb.valid_q == 0 || arb.last_q) ? valid_i : arb.valid_q;
        uint32_t req_d = arb.lock_q ? arb.req_q : valid_d;
        bool req_o = req_d != 0;
        int idx = req_o ? _this->arb_select(req_d, arb.rr_q) : 0;
        int sel = valid_i ? idx : 0;
        bool valid_o = (valid_i >> sel) & 1;
        bool ready_i = !output_full[out];
        bool last_out = false;

        if (valid_o && ready_i)
        {
            vp::Queue *queue = _this->input_queues[sel];
            FloonocReqV2 *req = (FloonocReqV2 *)queue->pop();
            last_out = req->is_last;
            if (queue->size() == _this->queue_capacity[sel] - 1)
            {
                _this->input_ports[sel]->unstall();
            }
            _this->output_queues[out]->push_back(req, 0);
        }
        else if (valid_o)
        {
            FloonocReqV2 *req = (FloonocReqV2 *)_this->input_queues[sel]->head();
            last_out = req->is_last;
        }

        bool gnt_i = ready_i && last_out;
        arb.lock_q = req_o && !gnt_i;
        arb.req_q = req_d;
        if (gnt_i && req_o)
        {
            arb.rr_q = _this->arb_next(req_d, arb.rr_q);
        }
        arb.valid_q = valid_d;
        arb.last_q = last_out && ready_i;
    }

    for (int i = 0; i < nb_ports; i++)
    {
        OutputArbiter &arb = _this->arbiters[i];
        if (_this->input_queues[i]->size() || _this->output_queues[i]->size() ||
            arb.valid_q || arb.lock_q || arb.last_q)
        {
            pending = true;
        }
    }
    if (pending)
    {
        _this->fsm_event.enqueue();
    }
}

int RouterV2::arb_select(uint32_t req, int rr)
{
    // rr_arb_tree: at each node, the right subtree when the left one has no
    // request, or when both have one and the rr bit of the level is set.
    int lo = 0;
    for (int level = 0; level < this->arb_levels; level++)
    {
        int half = 1 << (this->arb_levels - 1 - level);
        uint32_t left = (req >> lo) & ((1u << half) - 1);
        uint32_t right = (req >> (lo + half)) & ((1u << half) - 1);
        bool bit = (rr >> (this->arb_levels - 1 - level)) & 1;
        if (!left || (right && bit))
        {
            lo += half;
        }
    }
    return lo;
}

int RouterV2::arb_next(uint32_t req, int rr)
{
    // FairArb: the first request above rr, else the first one up to it
    for (int i = rr + 1; i < this->nb_ports; i++)
    {
        if ((req >> i) & 1) return i;
    }
    for (int i = 0; i <= rr; i++)
    {
        if ((req >> i) & 1) return i;
    }
    return rr;
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

        // XY routing, as the RTL floo_route_select: X first, then Y. An NI
        // of the West/East border (no router in its column) is reached
        // through the router it hangs off, with a last hop on X out of it.
        // The RTL routes on the NI's X and would eject into the border NI of
        // the source row; this only differs from it on such paths.
        int router_x = std::clamp<int64_t>(dest_x, this->cfg.router_min_x, this->cfg.router_max_x);
        if (router_x != x)
        {
            next_x = router_x < x ? x - 1 : x + 1;
            next_y = y;
        }
        else if (dest_y != y)
        {
            next_x = x;
            next_y = dest_y < y ? y - 1 : y + 1;
        }
        else
        {
            next_x = dest_x < x ? x - 1 : x + 1;
            next_y = y;
        }
    }
}

void RouterV2::link_unstall(vp::Block *__this, int output_id)
{
    RouterV2 *_this = (RouterV2 *)__this;
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (queue: %d)\n", output_id);
    *_this->stalled_queues[output_id] = false;
    _this->unstall_cycle[output_id] = _this->clock.get_cycles();
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
        for (int i = 0; i < this->nb_ports; i++)
        {
            *this->stalled_queues[i] = false;
            this->unstall_cycle[i] = -1;
            this->arbiters[i] = OutputArbiter();
        }
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new RouterV2(config);
}
