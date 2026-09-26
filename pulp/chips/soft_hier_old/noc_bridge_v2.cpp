// SPDX-License-Identifier: Apache-2.0
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <algorithm>
#include <deque>
#include <map>
#include <cstring>
#include "noc_bridge.hpp"

class NocBridgeV2 : public vp::Component
{
public:
    NocBridgeV2(vp::ComponentConf &config);
private:
    struct Source
    {
        SoftHierNocAccess *access;
        uint64_t submitted = 0;
        unsigned pending = 0;
        int64_t ready;
    };
    struct Fragment
    {
        Source *source;
        vp::IoReq *req;
        uint64_t offset, size, received = 0;
    };
    struct Target;
    struct Beat
    {
        Target *target;
        vp::IoReq *response;
        uint64_t offset;
        SoftHierNocAccess access;
    };
    struct Target
    {
        vp::IoReq *req;
        uint64_t addr, size;
        int64_t burst_id;
        void *initiator;
        bool write;
        uint64_t submitted = 0, returned = 0;
        std::map<uint64_t, Beat *> ready;
    };
    static void request(vp::Block *, SoftHierNocAccess *);
    static void completed(vp::Block *, SoftHierNocAccess *);
    static void tick(vp::Block *, vp::ClockEvent *);
    static vp::IoReqStatus input(vp::Block *, vp::IoReq *);
    static vp::IoRespAck response(vp::Block *, vp::IoReq *);
    static void retry(vp::Block *, vp::IoRetryChannel);
    static void response_retry(vp::Block *, vp::IoRetryChannel);
    void send_fragment(Fragment *fragment);
    void finish_fragment(Fragment *fragment, bool error);
    void send_response(Beat *beat);
    void retire_beat(Beat *beat);
    vp::IoMaster out;
    vp::IoSlave in;
    vp::WireMaster<SoftHierNocAccess *> request_out, done_out;
    vp::WireMaster<SoftHierCollective> collective_out;
    vp::WireSlave<SoftHierNocAccess *> request_in, done_in;
    vp::ClockEvent event;
    vp::Trace trace;
    vp::IoReqAllocator *zero_pool, *data_pool;
    uint64_t width, max_burst;
    unsigned capacity, buffered = 0;
    bool legacy_input, owes_retry = false;
    std::deque<Source *> sources;
    std::deque<Target *> targets;
    Fragment *denied = nullptr;
    Beat *denied_response = nullptr;
};

NocBridgeV2::NocBridgeV2(vp::ComponentConf &config)
    : vp::Component(config), out(&retry, &response), in(&input, &response_retry),
      event(this, &tick)
{
    traces.new_trace("trace", &trace, vp::DEBUG);
    width = get_js_config()->get_uint("width");
    max_burst = get_js_config()->get_uint("max_burst_size");
    capacity = get_js_config()->get_uint("capacity");
    legacy_input = get_js_config()->get("legacy_input")->get_bool();
    zero_pool = vp::IoReqAllocator::get(0);
    data_pool = vp::IoReqAllocator::get(width);
    if (legacy_input)
    {
        request_in.set_sync_meth(&request);
        new_slave_port("request", &request_in);
        new_master_port("done", &done_out);
        new_master_port("output", &out);
        new_master_port("collective", &collective_out);
    }
    else
    {
        done_in.set_sync_meth(&completed);
        new_slave_port("input", &in);
        new_master_port("request", &request_out);
        new_slave_port("done", &done_in);
    }
}

void NocBridgeV2::request(vp::Block *block, SoftHierNocAccess *access)
{
    auto *self = static_cast<NocBridgeV2 *>(block);
    auto *source = new Source{access, 0, 0,
        self->clock.get_cycles() + static_cast<int64_t>(access->latency)};
    self->sources.push_back(source);
    self->event.enqueue();
}

void NocBridgeV2::send_fragment(Fragment *fragment)
{
    vp::IoReq *req = fragment->req;
    auto collective = fragment->source->access->collective;
    collective.request = req;
    collective_out.sync(collective);
    vp::IoReqStatus status = out.req(req);
    if (status == vp::IO_REQ_DENIED)
        denied = fragment;
    else if (status == vp::IO_REQ_DONE)
    {
        // The mesh normally completes asynchronously; handle inline errors too.
        bool error = req->get_resp_status() != vp::IO_RESP_OK;
        if (!error && !fragment->source->access->write)
            trace.fatal("NoC bridge requires beat responses for successful reads\n");
        req->free();
        finish_fragment(fragment, error);
    }
}

void NocBridgeV2::finish_fragment(Fragment *fragment, bool error)
{
    Source *source = fragment->source;
    source->access->error |= error;
    source->pending--;
    delete fragment;
    if (source->submitted == source->access->size && source->pending == 0)
    {
        done_out.sync(source->access);
        delete source;
    }
    event.enqueue();
}

vp::IoRespAck NocBridgeV2::response(vp::Block *block, vp::IoReq *beat)
{
    auto *self = static_cast<NocBridgeV2 *>(block);
    auto *fragment = static_cast<Fragment *>(beat->initiator);
    bool write = fragment->source->access->write;
    bool last = beat->is_last;
    bool error = beat->get_resp_status() != vp::IO_RESP_OK;
    if (!write)
    {
        self->traces.assert(fragment->received + beat->get_size() <= fragment->size,
            "Oversized bridge read response");
        std::memcpy(fragment->source->access->data + fragment->offset + fragment->received,
            beat->get_data(), beat->get_size());
        fragment->received += beat->get_size();
        fragment->source->access->error |= error;
    }
    beat->free();
    if (last)
    {
        if (!write)
        {
            self->traces.assert(fragment->received == fragment->size || error,
                "Incomplete bridge read response");
            fragment->req->free();
        }
        self->finish_fragment(fragment, error);
    }
    // Original legacy requests own destination storage until completion, so
    // response data can always be consumed directly without an unbounded FIFO.
    return vp::IO_RESP_ACCEPTED;
}

void NocBridgeV2::retry(vp::Block *block, vp::IoRetryChannel)
{
    auto *self = static_cast<NocBridgeV2 *>(block);
    if (self->denied)
    {
        Fragment *fragment = self->denied;
        self->denied = nullptr;
        self->send_fragment(fragment); // Retry must be synchronous.
    }
    self->event.enqueue();
}

vp::IoReqStatus NocBridgeV2::input(vp::Block *block, vp::IoReq *req)
{
    auto *self = static_cast<NocBridgeV2 *>(block);
    if ((req->get_opcode() != vp::READ && req->get_opcode() != vp::WRITE) ||
        req->get_size() == 0)
    {
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }
    if (self->targets.size() >= self->capacity)
    {
        self->owes_retry = true;
        return vp::IO_REQ_DENIED;
    }
    bool write = req->get_opcode() == vp::WRITE;
    if (write && (req->get_size() > self->width || !req->is_first || !req->is_last))
        self->trace.fatal("NoC target must provide individual write beats\n");
    auto *target = new Target{req, req->get_addr(), req->get_size(), req->burst_id,
        req->initiator, write};
    self->targets.push_back(target);
    self->event.enqueue();
    return vp::IO_REQ_GRANTED;
}

void NocBridgeV2::completed(vp::Block *block, SoftHierNocAccess *access)
{
    auto *self = static_cast<NocBridgeV2 *>(block);
    auto *beat = static_cast<Beat *>(access->owner);
    beat->target->ready.emplace(beat->offset, beat);
    self->event.enqueue();
}

void NocBridgeV2::retire_beat(Beat *beat)
{
    Target *target = beat->target;
    target->ready.erase(beat->offset);
    target->returned += beat->access.size;
    buffered--;
    delete beat;
    if (target->returned == target->size)
    {
        targets.erase(std::find(targets.begin(), targets.end(), target));
        delete target;
        if (owes_retry)
        {
            owes_retry = false;
            in.retry();
        }
    }
    event.enqueue();
}

void NocBridgeV2::send_response(Beat *beat)
{
    if (in.resp(beat->response) == vp::IO_RESP_DENIED)
        denied_response = beat;
    else
        retire_beat(beat);
}

void NocBridgeV2::response_retry(vp::Block *block, vp::IoRetryChannel)
{
    auto *self = static_cast<NocBridgeV2 *>(block);
    if (self->denied_response)
    {
        Beat *beat = self->denied_response;
        self->denied_response = nullptr;
        self->send_response(beat);
    }
}

void NocBridgeV2::tick(vp::Block *block, vp::ClockEvent *)
{
    auto *self = static_cast<NocBridgeV2 *>(block);
    if (self->legacy_input)
    {
        if (!self->sources.empty() && !self->denied)
        {
            Source *source = self->sources.front();
            auto *access = source->access;
            if (source->ready > self->clock.get_cycles())
            {
                self->event.enqueue(source->ready - self->clock.get_cycles());
                return;
            }
            if (access->size == 0)
            {
                self->sources.pop_front();
                self->done_out.sync(access);
                delete source;
            }
            else
            {
                uint64_t addr = access->addr + source->submitted;
                // One collective fragment is one link beat. This bounds tree
                // reduction storage and gives every beat its own completion.
                uint64_t boundary = (access->write || access->collective.type) ? self->width : self->max_burst;
                uint64_t size = std::min(access->size - source->submitted, boundary - addr % boundary);
                vp::IoReq *req = self->zero_pool->alloc();
                req->prepare();
                req->set_addr(addr);
                req->set_size(size);
                req->set_is_write(access->write);
                req->set_data(access->write ? access->data + source->submitted : nullptr);
                req->set_second_data(nullptr);
                req->is_first = req->is_last = true;
                req->burst_id = -1;
                auto *fragment = new Fragment{source, req, source->submitted, size};
                req->initiator = fragment;
                source->submitted += size;
                source->pending++;
                if (source->submitted == access->size)
                    self->sources.pop_front();
                self->send_fragment(fragment);
            }
        }
        if (!self->sources.empty() && !self->denied)
            self->event.enqueue();
        return;
    }

    // At most one legacy beat per cycle, with a bounded total number of
    // outstanding/ready beats. Preserve the existing HBM controller's striping.
    if (self->buffered < self->capacity)
    {
        for (Target *target : self->targets)
        {
            if (target->submitted == target->size) continue;
            uint64_t offset = target->submitted;
            uint64_t addr = target->addr + offset;
            uint64_t size = std::min(target->size - offset, self->width - addr % self->width);
            vp::IoReq *response = target->write ? self->zero_pool->alloc() : self->data_pool->alloc();
            response->prepare();
            if (target->write) response->set_data(nullptr);
            response->set_addr(addr);
            response->set_size(size);
            response->set_is_write(target->write);
            response->is_first = offset == 0;
            response->is_last = offset + size == target->size;
            response->burst_id = target->burst_id;
            response->initiator = target->initiator;
            auto *beat = new Beat{target, response, offset,
                {addr, size, target->write ? target->req->get_data() + offset : response->get_data(),
                 target->write}};
            beat->access.owner = beat;
            target->submitted += size;
            self->buffered++;
            self->request_out.sync(&beat->access);
            break;
        }
    }
    if (!self->denied_response)
    {
        for (Target *target : self->targets)
        {
            auto it = target->ready.find(target->returned);
            if (it == target->ready.end()) continue;
            Beat *beat = it->second;
            beat->response->set_resp_status(beat->access.error ? vp::IO_RESP_INVALID : vp::IO_RESP_OK);
            if (target->write) target->req->free();
            self->send_response(beat);
            break;
        }
    }
    // Pending legacy responses will wake us. Only poll while work is issuable.
    for (Target *target : self->targets)
    {
        if ((self->buffered < self->capacity && target->submitted < target->size) ||
            (!self->denied_response && target->ready.count(target->returned)))
        {
            self->event.enqueue();
            break;
        }
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new NocBridgeV2(config);
}
