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
#include "collective_reduction.hpp"

static const char *dir_names[SoftHierRouterV2::DIR_NB] = {"right", "left", "up", "down", "local"};

SoftHierRouterV2::SoftHierRouterV2(vp::ComponentConf &config)
    : vp::Component(config),
      fsm_event(this, &SoftHierRouterV2::fsm_handler),
      collective_ready(this, "collective_ready", &fsm_event),
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
        SoftHierFloonocLinkSlave(DIR_RIGHT, &SoftHierRouterV2::link_req),
        SoftHierFloonocLinkSlave(DIR_LEFT, &SoftHierRouterV2::link_req),
        SoftHierFloonocLinkSlave(DIR_UP, &SoftHierRouterV2::link_req),
        SoftHierFloonocLinkSlave(DIR_DOWN, &SoftHierRouterV2::link_req),
        SoftHierFloonocLinkSlave(DIR_LOCAL, &SoftHierRouterV2::link_req)
      }},
      output_ports{{
        SoftHierFloonocLinkMaster(DIR_RIGHT, &SoftHierRouterV2::link_unstall),
        SoftHierFloonocLinkMaster(DIR_LEFT, &SoftHierRouterV2::link_unstall),
        SoftHierFloonocLinkMaster(DIR_UP, &SoftHierRouterV2::link_unstall),
        SoftHierFloonocLinkMaster(DIR_DOWN, &SoftHierRouterV2::link_unstall),
        SoftHierFloonocLinkMaster(DIR_LOCAL, &SoftHierRouterV2::link_unstall)
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

SoftHierRouterV2::~SoftHierRouterV2()
{
    for (int i = 0; i < DIR_NB; i++)
    {
        delete this->input_queues[i];
    }
}

bool SoftHierRouterV2::link_req(vp::Block *__this, SoftHierFloonocReqV2 *req, int queue_index)
{
    SoftHierRouterV2 *_this = (SoftHierRouterV2 *)__this;

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

// The extra arbitration input contains completed reductions/ack joins.
// No collective traffic: its empty slot leaves the original round robin order.
void SoftHierRouterV2::fsm_handler(vp::Block *block, vp::ClockEvent *)
{
    auto *self = static_cast<SoftHierRouterV2 *>(block);
    bool output_full[DIR_NB] = {};
    int first = self->current_queue;
    for (int n = 0; n <= DIR_NB; ++n)
    {
        int input = (first + n) % (DIR_NB + 1);
        vp::Queue *queue = input == DIR_NB ? &self->collective_ready : self->input_queues[input];
        if (queue->empty()) continue;
        auto *req = static_cast<SoftHierFloonocReqV2 *>(queue->head());
        auto pop = [&]() {
            queue->pop();
            if (input < DIR_NB && queue->size() == self->queue_size) self->input_ports[input].unstall();
            self->current_queue = (input + 1) % (DIR_NB + 1);
        };
        if (req->collective.type && !req->is_rsp)
        {
            if (self->collective_forward(req, input, output_full))
            {
                pop();
                if (req->get_is_write() && req->is_address) SoftHierFloonocReqV2Allocator::get()->free(req);
            }
        }
        else if (req->collective.type && req->collective_parent &&
                 req->dest_x == self->x && req->dest_y == self->y)
        {
            pop();
            self->collective_reply(req);
        }
        else
        {
            int nx, ny;
            self->get_next_router_pos(req->dest_x, req->dest_y, nx, ny);
            int output = self->get_req_queue(nx, ny);
            if (output_full[output] || self->stalled_queues[output] ||
                (self->output_owner[output] != -1 && self->output_owner[output] != input)) continue;
            output_full[output] = true;
            self->output_owner[output] = req->is_last ? -1 : input;
            self->trace.msg(vp::Trace::LEVEL_DEBUG,
                "NOC_V2_HOP req=%p from=(%d,%d) to=(%d,%d)\n", req, self->x, self->y, nx, ny);
            pop();
            self->stalled_queues[output] = self->output_ports[output].req(req);
        }
    }
    for (int input = 0; input <= DIR_NB; ++input)
    {
        auto *queue = input == DIR_NB ? &self->collective_ready : self->input_queues[input];
        if (queue->size()) { self->fsm_event.enqueue(); break; }
    }
}

int SoftHierRouterV2::collective_routes(SoftHierFloonocReqV2 *req)
{
    auto selected = [req](int x, int y) {
        return (((x - 1) & req->collective.row_mask) == ((req->src_x - 1) & req->collective.row_mask)) &&
               (((y - 1) & req->collective.col_mask) == ((req->src_y - 1) & req->collective.col_mask));
    };
    int routes = selected(x, y) ? 1 << DIR_LOCAL : 0;
    int momentum = req->collective_momentum;
    if (momentum == DIR_LOCAL || momentum == DIR_RIGHT)
        for (int next = x + 1; next < dim_x - 1; ++next)
            if (selected(next, y)) { routes |= 1 << DIR_RIGHT; break; }
    if (momentum == DIR_LOCAL || momentum == DIR_LEFT)
        for (int next = x - 1; next > 0; --next)
            if (selected(next, y)) { routes |= 1 << DIR_LEFT; break; }
    if (momentum != DIR_DOWN)
        for (int next = y + 1; next < dim_y - 1; ++next)
            if (selected(x, next)) { routes |= 1 << DIR_UP; break; }
    if (momentum != DIR_UP)
        for (int next = y - 1; next > 0; --next)
            if (selected(x, next)) { routes |= 1 << DIR_DOWN; break; }
    return routes;
}

bool SoftHierRouterV2::collective_forward(SoftHierFloonocReqV2 *req, int input, bool *output_full)
{
    auto *pool = SoftHierFloonocReqV2Allocator::get();
    bool address_only = req->get_is_write() && req->is_address;
    if (req->collective_outputs < 0)
    {
        req->collective_outputs = this->collective_routes(req);
        this->traces.assert(req->collective_outputs != 0, "Collective branch has no participant");
        if (!address_only)
        {
            auto join = std::make_shared<SoftHierCollectiveJoin>();
            join->request = req; join->x = this->x; join->y = this->y;
            for (int dir = 0; dir < DIR_NB; ++dir) join->pending += (req->collective_outputs >> dir) & 1;
            req->collective_fork = join;
        }
    }
    for (int dir = 0; dir < DIR_NB; ++dir)
    {
        if (!(req->collective_outputs & (1 << dir)) || output_full[dir] || this->stalled_queues[dir] ||
            (this->output_owner[dir] != -1 && this->output_owner[dir] != input)) continue;
        auto *child = pool->clone(req);
        child->collective_parent = req->collective_fork;
        child->collective_fork.reset();
        child->collective_outputs = -1;
        child->collective_momentum = dir;
        child->collective_slot = dir;
        child->dest_x = this->x + (dir == DIR_RIGHT) - (dir == DIR_LEFT);
        child->dest_y = this->y + (dir == DIR_UP) - (dir == DIR_DOWN);
        child->is_first = child->is_last = true;
        req->collective_outputs &= ~(1 << dir);
        output_full[dir] = true;
        this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "NOC_V2_FORK parent=%p req=%p from=(%d,%d) to=(%d,%d) op=%u bytes=%lu address=%d\n",
            req, child, this->x, this->y, child->dest_x, child->dest_y,
            req->collective.type, req->get_size(), req->is_address);
        this->stalled_queues[dir] = this->output_ports[dir].req(child);
    }
    if (req->collective_outputs) return false;
    req->collective_fork.reset(); // now owned by the outstanding branches
    return true;
}

void SoftHierRouterV2::collective_reply(SoftHierFloonocReqV2 *req)
{
    auto join = req->collective_parent;
    this->traces.assert(join->x == x && join->y == y && !join->replies[req->collective_slot],
        "Duplicate or misrouted collective contribution");
    join->replies[req->collective_slot] = req;
    if (--join->pending) return;
    auto *result = join->request;
    bool seeded = false, error = false;
    // Fixed tree order makes FP16 rounding independent of target latency.
    for (int slot : {DIR_LOCAL, DIR_RIGHT, DIR_LEFT, DIR_UP, DIR_DOWN})
    {
        auto *reply = join->replies[slot];
        if (!reply) continue;
        error |= reply->get_resp_status() != vp::IO_RESP_OK;
        if (!result->get_is_write())
        {
            this->traces.assert(reply->get_size() == result->get_size(), "Collective response size mismatch");
            if (!seeded)
            {
                result->payload.assign(result->get_size(), 0);
                result->set_data(result->payload.data());
                if (reply->get_data()) std::memcpy(result->get_data(), reply->get_data(), result->get_size());
                seeded = true;
            }
            else if (reply->get_data())
                softhier_collective::combine(result->collective.type, result->get_data(), reply->get_data(), result->get_size());
        }
        SoftHierFloonocReqV2Allocator::get()->free(reply);
    }
    result->is_rsp = true;
    result->is_address = result->get_is_write();
    result->is_first = result->is_last = true;
    result->set_resp_status(error ? vp::IO_RESP_INVALID : vp::IO_RESP_OK);
    result->dest_x = result->collective_parent ? result->collective_parent->x : result->src_x;
    result->dest_y = result->collective_parent ? result->collective_parent->y : result->src_y;
    this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "NOC_V2_JOIN req=%p at=(%d,%d) to=(%d,%d) op=%u bytes=%lu write=%d\n",
        result, x, y, result->dest_x, result->dest_y, result->collective.type,
        result->get_size(), result->get_is_write());
    // One pipeline cycle for the join. The result then uses normal arbitration.
    this->collective_ready.push_back(result, 1);
}

void SoftHierRouterV2::get_next_router_pos(int dest_x, int dest_y, int &next_x, int &next_y)
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

void SoftHierRouterV2::link_unstall(vp::Block *__this, int output_id)
{
    SoftHierRouterV2 *_this = (SoftHierRouterV2 *)__this;
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue (queue: %d)\n", output_id);
    _this->stalled_queues[output_id] = false;
    _this->fsm_event.enqueue();
}

int SoftHierRouterV2::get_req_queue(int from_x, int from_y)
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

void SoftHierRouterV2::reset(bool active)
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
    return new SoftHierRouterV2(config);
}
