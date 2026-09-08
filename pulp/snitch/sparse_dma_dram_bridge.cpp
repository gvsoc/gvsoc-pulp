// SPDX-License-Identifier: Apache-2.0
// Match dram_rtl_sim's AXI-to-AXI-Lite bridge: 512-bit read beats, including
// narrow core/cache reads, and a bounded window of outstanding beats.
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>

class SparseDmaDramBridge : public vp::Component
{
    struct Transfer {
        vp::IoReq *req;
        uint64_t issued = 0, pending = 0;
        int64_t ready;
    };
    struct Beat {
        vp::IoReq req;
        Transfer *parent;
        uint64_t offset, count, skip;
        uint8_t data[64];
    };
    vp::IoSlave input;
    vp::IoMaster output;
    vp::ClockEvent event;
    std::deque<Transfer *> queue;
    std::unordered_map<vp::IoReq *, Beat *> inflight;
    unsigned max_inflight;
    bool blocked = false;
    vp::Trace trace;

    static vp::IoReqStatus request(vp::Block *block, vp::IoReq *req)
    {
        auto self = static_cast<SparseDmaDramBridge *>(block);
        if (req->is_debug()) return self->output.req(req);
        if (req->get_size() == 0) return vp::IO_REQ_OK;
        auto transfer = new Transfer;
        transfer->req = req;
        transfer->ready = self->clock.get_cycles() + req->get_latency();
        // Consume the fabric delay before submitting to the timed controller.
        req->set_latency(0);
        self->queue.push_back(transfer);
        self->event.enqueue();
        return vp::IO_REQ_PENDING;
    }
    static void grant(vp::Block *block, vp::IoReq *)
    {
        auto self = static_cast<SparseDmaDramBridge *>(block);
        self->blocked = false;
        self->event.enqueue();
    }
    static void response(vp::Block *block, vp::IoReq *req)
    { static_cast<SparseDmaDramBridge *>(block)->complete(req); }
    void complete(vp::IoReq *req)
    {
        auto beat = inflight.at(req);
        auto parent = beat->parent;
        if (!req->get_is_write())
            std::memcpy(parent->req->get_data() + beat->offset, beat->data + beat->skip, beat->count);
        --parent->pending;
        inflight.erase(req);
        delete beat;
        if (parent->pending == 0 && parent->issued == parent->req->get_size())
        {
            auto original = parent->req;
            delete parent;
            original->get_resp_port()->resp(original);
        }
        if (!queue.empty()) event.enqueue();
    }
    static void step(vp::Block *block, vp::ClockEvent *)
    {
        auto self = static_cast<SparseDmaDramBridge *>(block);
        if (self->blocked || self->queue.empty() || self->inflight.size() >= self->max_inflight)
            return;
        auto parent = self->queue.front();
        auto now = self->clock.get_cycles();
        if (parent->ready > now)
        {
            self->event.enqueue(parent->ready - now);
            return;
        }
        auto beat = new Beat;
        auto address = parent->req->get_addr() + parent->issued;
        beat->parent = parent;
        beat->offset = parent->issued;
        beat->skip = address & 63;
        beat->count = std::min<uint64_t>(64 - beat->skip, parent->req->get_size() - parent->issued);
        beat->req.prepare();
        beat->req.set_is_write(parent->req->get_is_write());
        if (parent->req->get_is_write())
        {
            // Keep the valid byte range so DRAMSys generates write strobes.
            beat->req.set_addr(address);
            beat->req.set_size(beat->count);
            beat->req.set_data(parent->req->get_data() + beat->offset);
        }
        else
        {
            beat->req.set_addr(address & ~uint64_t(63));
            beat->req.set_size(64);
            beat->req.set_data(beat->data);
        }
        parent->issued += beat->count;
        ++parent->pending;
        if (parent->issued == parent->req->get_size()) self->queue.pop_front();
        self->inflight.emplace(&beat->req, beat);
        self->trace.msg(vp::Trace::LEVEL_TRACE, "DRAM_BEAT cycle=%lld addr=0x%llx size=%llu\n",
            now, beat->req.get_addr(), beat->req.get_size());
        auto status = self->output.req(&beat->req);
        if (status == vp::IO_REQ_OK) self->complete(&beat->req);
        else if (status == vp::IO_REQ_DENIED) self->blocked = true;
        else if (status == vp::IO_REQ_INVALID) self->trace.fatal("Invalid DRAM beat\n");
        if (!self->queue.empty() && !self->blocked) self->event.enqueue();
    }
public:
    SparseDmaDramBridge(vp::ComponentConf &config) : vp::Component(config), event(this, step)
    {
        max_inflight = get_js_config()->get_int("max_inflight");
        traces.new_trace("trace", &trace, vp::DEBUG);
        input.set_req_meth(request);
        new_slave_port("input", &input);
        output.set_resp_meth(response);
        output.set_grant_meth(grant);
        new_master_port("output", &output);
    }
};
extern "C" vp::Component *gv_new(vp::ComponentConf &conf) { return new SparseDmaDramBridge(conf); }
