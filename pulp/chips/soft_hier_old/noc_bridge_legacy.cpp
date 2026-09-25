// SPDX-License-Identifier: Apache-2.0
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <map>
#include <unordered_map>
#include <cstring>
#include "noc_bridge.hpp"

class NocBridgeLegacy : public vp::Component
{
public:
    NocBridgeLegacy(vp::ComponentConf &config);
private:
    struct Origin { vp::IoReq *req; SoftHierNocAccess access; };
    static vp::IoReqStatus input(vp::Block *, vp::IoReq *);
    static void completed(vp::Block *, SoftHierNocAccess *);
    static void request(vp::Block *, SoftHierNocAccess *);
    static void response(vp::Block *, vp::IoReq *);
    static void grant(vp::Block *, vp::IoReq *) {} // v1 DENIED is already queued.
    static void tick(vp::Block *, vp::ClockEvent *);
    void finish_later(vp::IoReq *req);
    vp::IoSlave in;
    vp::IoMaster out;
    vp::WireMaster<SoftHierNocAccess *> request_out, done_out;
    vp::WireSlave<SoftHierNocAccess *> request_in, done_in;
    vp::ClockEvent event;
    vp::Trace trace;
    std::unordered_map<vp::IoReq *, SoftHierNocAccess *> pending;
    std::multimap<int64_t, vp::IoReq *> ready;
};

NocBridgeLegacy::NocBridgeLegacy(vp::ComponentConf &config)
    : vp::Component(config), event(this, &NocBridgeLegacy::tick)
{
    traces.new_trace("trace", &trace, vp::DEBUG);
    if (get_js_config()->get("legacy_input")->get_bool())
    {
        in.set_req_meth(&input);
        done_in.set_sync_meth(&completed);
        new_slave_port("input", &in);
        new_master_port("request", &request_out);
        new_slave_port("done", &done_in);
    }
    else
    {
        out.set_resp_meth(&response);
        out.set_grant_meth(&grant);
        request_in.set_sync_meth(&request);
        new_master_port("output", &out);
        new_slave_port("request", &request_in);
        new_master_port("done", &done_out);
    }
}

vp::IoReqStatus NocBridgeLegacy::input(vp::Block *block, vp::IoReq *req)
{
    auto *self = static_cast<NocBridgeLegacy *>(block);
    if (req->get_opcode() != vp::READ && req->get_opcode() != vp::WRITE)
        self->trace.fatal("SoftHier data_noc v2 bridge supports unicast reads/writes only\n");
    auto *origin = new Origin{req, {req->get_addr(), req->get_size(),
        req->get_data(), req->get_is_write(), false, req->get_full_latency(), nullptr}};
    origin->access.owner = origin;
    // The v2 face dispatches on a clock event, after this PENDING returns.
    self->request_out.sync(&origin->access);
    return vp::IO_REQ_PENDING;
}

void NocBridgeLegacy::completed(vp::Block *, SoftHierNocAccess *access)
{
    auto *origin = static_cast<Origin *>(access->owner);
    vp::IoReq *req = origin->req;
    req->prepare(); // Latency has elapsed in the bridge/mesh, not an annotation.
    req->status = access->error ? vp::IO_REQ_INVALID : vp::IO_REQ_OK;
    delete origin;
    req->get_resp_port()->resp(req);
}

void NocBridgeLegacy::request(vp::Block *block, SoftHierNocAccess *access)
{
    auto *self = static_cast<NocBridgeLegacy *>(block);
    auto *req = new vp::IoReq(access->addr, access->data, access->size, access->write);
    req->status = vp::IO_REQ_OK;
    req->set_second_data(nullptr);
    std::memset(req->get_payload(), 0, req->get_payload_size());
    self->pending.emplace(req, access);
    vp::IoReqStatus status = self->out.req(req);
    if (status == vp::IO_REQ_OK || status == vp::IO_REQ_INVALID)
    {
        req->status = status;
        self->finish_later(req);
    }
    // PENDING and DENIED both complete through response(), with no resubmission.
}

void NocBridgeLegacy::finish_later(vp::IoReq *req)
{
    // Also defer zero-latency targets: neither protocol may complete an async
    // transaction recursively before the caller has recorded its acceptance.
    int64_t delay = std::max<uint64_t>(1, req->get_full_latency());
    ready.emplace(clock.get_cycles() + delay, req);
    event.enqueue(std::max<int64_t>(1, ready.begin()->first - clock.get_cycles()));
}

void NocBridgeLegacy::response(vp::Block *block, vp::IoReq *req)
{
    static_cast<NocBridgeLegacy *>(block)->finish_later(req);
}

void NocBridgeLegacy::tick(vp::Block *block, vp::ClockEvent *)
{
    auto *self = static_cast<NocBridgeLegacy *>(block);
    while (!self->ready.empty() && self->ready.begin()->first <= self->clock.get_cycles())
    {
        vp::IoReq *req = self->ready.begin()->second;
        self->ready.erase(self->ready.begin());
        auto it = self->pending.find(req);
        self->traces.assert(it != self->pending.end(), "Unknown legacy completion");
        auto *access = it->second;
        access->error = req->status == vp::IO_REQ_INVALID;
        self->pending.erase(it);
        delete req;
        self->done_out.sync(access);
    }
    if (!self->ready.empty())
        self->event.enqueue(std::max<int64_t>(1, self->ready.begin()->first - self->clock.get_cycles()));
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new NocBridgeLegacy(config);
}
