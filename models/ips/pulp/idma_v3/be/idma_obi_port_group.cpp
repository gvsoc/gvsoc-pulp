/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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
 * Authors: Germain Haugou (germain.haugou@gmail.com)
 */

#include <algorithm>
#include <cstring>
#include <vp/vp.hpp>
#include "idma_obi_port_group.hpp"



// ---------------------------------------------------------------------------
// Port group
// ---------------------------------------------------------------------------

IdmaObiPortGroup::IdmaObiPortGroup(vp::Component *idma, std::string name, int nb_ports,
    int port_width, uint64_t addr_mask)
:   Block(idma, name),
    fsm_event(this, &IdmaObiPortGroup::fsm_handler),
    done_event(this, &IdmaObiPortGroup::done_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->nb_ports = nb_ports;
    this->port_width = port_width;
    this->addr_mask = addr_mask;

    for (int i = 0; i < nb_ports; i++)
    {
        vp::IoMaster *port = new vp::IoMaster(i, &IdmaObiPortGroup::retry_meth,
            &IdmaObiPortGroup::resp_meth);
        idma->new_master_port(name + "_" + std::to_string(i), port, this);
        this->ports.push_back(port);
        this->reqs.push_back(new vp::IoReq());
    }

    this->port_denied.resize(nb_ports);
    this->port_pending.resize(nb_ports);

    this->rr_next = 0;
    this->issuing = false;
    this->nb_pending_grants = 0;
    this->next_issue_cycle = 0;
}



void IdmaObiPortGroup::reset(bool active)
{
    if (active)
    {
        this->rr_next = 0;
        this->issuing = false;
        this->nb_pending_grants = 0;
        this->next_issue_cycle = 0;
        this->pending_done.clear();
        for (int i = 0; i < this->nb_ports; i++)
        {
            this->port_denied[i] = false;
            this->port_pending[i] = false;
        }
    }
}



void IdmaObiPortGroup::add_client(IdmaObiClient *client)
{
    this->clients.push_back(client);
}



void IdmaObiPortGroup::update()
{
    this->arbitrate();
}



void IdmaObiPortGroup::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IdmaObiPortGroup *_this = (IdmaObiPortGroup *)__this;
    _this->arbitrate();
}



void IdmaObiPortGroup::arbitrate()
{
    int64_t cycles = this->clock.get_cycles();

    // One access at a time until all its ports are granted, and at most one
    // per cycle
    if (this->issuing)
    {
        return;
    }

    if (cycles < this->next_issue_cycle)
    {
        this->fsm_event.enqueue(this->next_issue_cycle - cycles);
        return;
    }

    int nb_clients = this->clients.size();
    for (int i = 0; i < nb_clients; i++)
    {
        int index = (this->rr_next + i) % nb_clients;
        IdmaObiClient *client = this->clients[index];
        if (client->obi_has_word())
        {
            this->rr_next = (index + 1) % nb_clients;
            client->obi_issue_word();
            return;
        }
    }
}



void IdmaObiPortGroup::send(IdmaObiClient *client, void *token, uint64_t addr, uint8_t *data,
    uint64_t size, bool is_write)
{
    int access_width = this->access_width();
    uint64_t base = addr & ~((uint64_t)access_width - 1);

    this->traces.assert(!this->issuing, "OBI access issued while another one is being granted");
    this->traces.assert(addr + size <= base + access_width,
        "OBI access crosses an access boundary (addr: 0x%lx, size: 0x%lx)", addr, size);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Issuing %s access (addr: 0x%lx, size: 0x%lx)\n",
        is_write ? "write" : "read", addr, size);

    this->issuing = true;
    this->current.client = client;
    this->current.token = token;
    this->current.done_cycle = this->clock.get_cycles();
    this->nb_pending_grants = 0;

    // Split the access over the ports: port i holds bytes [i*w, (i+1)*w) of
    // the aligned word, the access only touches the ports it covers
    for (int i = 0; i < this->nb_ports; i++)
    {
        uint64_t lo = std::max(addr, base + (uint64_t)i * this->port_width);
        uint64_t hi = std::min(addr + size, base + (uint64_t)(i + 1) * this->port_width);

        if (hi <= lo)
        {
            this->port_pending[i] = false;
            continue;
        }

        vp::IoReq *req = this->reqs[i];
        req->prepare();
        req->set_addr(lo & this->addr_mask);
        req->set_size(hi - lo);
        req->set_is_write(is_write);
        req->set_data(data + (lo - addr));
        this->port_pending[i] = true;
        this->port_denied[i] = false;
        this->nb_pending_grants++;
    }

    for (int i = 0; i < this->nb_ports; i++)
    {
        if (!this->port_pending[i])
        {
            continue;
        }

        vp::IoReq *req = this->reqs[i];
        this->traces.declare_access(req->get_addr(), req->get_size(), is_write);
        vp::IoReqStatus status = this->ports[i]->req(req);
        if (status == vp::IO_REQ_DONE)
        {
            this->port_granted(i);
        }
        else if (status == vp::IO_REQ_DENIED)
        {
            // Bank conflict: the request is held, the crossbar calls retry()
            // once this port wins its election
            this->port_denied[i] = true;
        }
        else
        {
            this->trace.fatal("OBI port group expects inline responses (port: %d)\n", i);
        }
    }
}



void IdmaObiPortGroup::port_granted(int id)
{
    vp::IoReq *req = this->reqs[id];

    if (req->get_resp_status() == vp::IO_RESP_INVALID)
    {
        this->trace.force_warning("Invalid access on OBI port (port: %d, addr: 0x%lx, "
            "size: 0x%lx)\n", id, req->get_addr(), req->get_size());
    }

    this->port_pending[id] = false;
    this->port_denied[id] = false;
    this->nb_pending_grants--;

    // The word is valid once the slowest port has answered
    int64_t done_cycle = this->clock.get_cycles() + req->get_full_latency();
    if (done_cycle > this->current.done_cycle)
    {
        this->current.done_cycle = done_cycle;
    }

    if (this->nb_pending_grants == 0)
    {
        this->issuing = false;
        this->next_issue_cycle = this->clock.get_cycles() + 1;
        this->pending_done.push_back(this->current);
        this->schedule_done();
        this->current.client->obi_word_granted(this->current.token);
        this->fsm_event.enqueue(1);
    }
}



void IdmaObiPortGroup::schedule_done()
{
    if (this->pending_done.size() > 0)
    {
        int64_t delay = this->pending_done.front().done_cycle - this->clock.get_cycles();
        if (delay < 0) delay = 0;
        this->done_event.enqueue(delay);
    }
}



void IdmaObiPortGroup::done_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IdmaObiPortGroup *_this = (IdmaObiPortGroup *)__this;
    int64_t cycles = _this->clock.get_cycles();

    while (_this->pending_done.size() > 0 && _this->pending_done.front().done_cycle <= cycles)
    {
        Access access = _this->pending_done.front();
        _this->pending_done.pop_front();
        access.client->obi_word_done(access.token);
    }

    _this->schedule_done();
}



void IdmaObiPortGroup::retry_meth(vp::Block *__this, int id, vp::IoRetryChannel channel)
{
    IdmaObiPortGroup *_this = (IdmaObiPortGroup *)__this;

    if (!_this->port_denied[id])
    {
        return;
    }

    // The crossbar only forwards inline during its election: re-issue the
    // held request from here, in the same cycle
    vp::IoReqStatus status = _this->ports[id]->req(_this->reqs[id]);
    if (status == vp::IO_REQ_DONE)
    {
        _this->port_granted(id);
    }
    else if (status != vp::IO_REQ_DENIED)
    {
        _this->trace.fatal("OBI port group expects inline responses (port: %d)\n", id);
    }
}



vp::IoRespAck IdmaObiPortGroup::resp_meth(vp::Block *__this, vp::IoReq *req, int id)
{
    IdmaObiPortGroup *_this = (IdmaObiPortGroup *)__this;
    _this->trace.fatal("OBI port group received an asynchronous response (port: %d)\n", id);
    return vp::IO_RESP_ACCEPTED;
}

