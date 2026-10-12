/*
 * Copyright (C) 2025 Fondazione Chips-IT
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
 * Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <deque>
#include <unordered_map>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>

/*
 * RTL obi_cut (a spill register on the A and on the R channel) in front of a
 * register slave, from the beat plane of the crossbar to an IoV2SingleReq
 * slave.
 *
 * A request taken at cycle t reaches the slave at t + 1. A slave answering
 * inline (a register file whose rvalid is combinational, like the RTL
 * idma_obi_ctrl_decoder) has its response back to the master at t + 2, plus
 * resp_latency for a slave whose response is registered; an asynchronous
 * response goes back one cycle after the slave sends it.
 *
 * This replaces a framework cut followed by the framework beat-to-single-req
 * adapter, which always answers at least one cycle after the slave even when
 * the slave answered inline, i.e. one cycle more than the RTL on every
 * register access (core/models/utils/io_v2_beat_to_single_req_adapter.cpp).
 *
 * Reads and writes travel on two channels, each taking one request and giving
 * back one response per cycle, as the cut. Only single-beat accesses are
 * supported (register accesses); the atomics are round-tripped as on any
 * beat binding.
 */

class ObiCut : public vp::Component
{
public:
    ObiCut(vp::ComponentConf &config);
    void reset(bool active) override;

private:
    enum { CH_READ = 0, CH_WRITE = 1, NB_CHANNELS = 2 };

    // An access between the master and the slave. down is what is sent to
    // the slave and what goes back upstream: the read response beat (from the
    // allocator, filled by the slave), the write beat (granted to us), or the
    // master's own request for an atomic.
    struct Access {
        vp::IoReq *up;
        vp::IoReq *down;
        int channel;
        int64_t ready;
    };

    static vp::IoReqStatus in_req(vp::Block *__this, vp::IoReq *req);
    static void in_resp_retry(vp::Block *__this, vp::IoRetryChannel channel);
    static vp::IoRespAck out_resp(vp::Block *__this, vp::IoReq *req);
    static void out_retry(vp::Block *__this, vp::IoRetryChannel channel);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    static int channel_of(vp::IoReq *req)
    {
        return req->get_opcode() == vp::WRITE ? CH_WRITE : CH_READ;
    }
    void send_down(int channel);
    void respond(Access &access, int64_t latency);
    void send_up(int channel);
    void schedule();

    vp::Trace trace;
    vp::IoSlave in{&ObiCut::in_req, &ObiCut::in_resp_retry};
    vp::IoMaster out{&ObiCut::out_retry, &ObiCut::out_resp};
    vp::ClockEvent *fsm_event;

    int width;
    int resp_latency;
    vp::IoReqAllocator *beat_allocator;

    // Requests taken from the master, in order, and responses to give back
    std::deque<Access> req_queue[NB_CHANNELS];
    std::deque<Access> resp_queue[NB_CHANNELS];
    // Requests granted by the slave, waiting for its response
    std::unordered_map<vp::IoReq *, Access> pending;
    // Set when the slave denied the head request of the channel
    bool down_denied[NB_CHANNELS];
    // Set when the master denied the head response of the channel
    bool up_denied[NB_CHANNELS];
    // Set when a request was denied to the master, which waits for a retry
    bool in_denied[NB_CHANNELS];
};

// Entries of the spill register of each channel
#define OBI_CUT_DEPTH 2

ObiCut::ObiCut(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_slave_port("input", &this->in);
    this->new_master_port("output", &this->out);

    this->fsm_event = this->event_new(&ObiCut::fsm_handler);

    this->width = this->get_js_config()->get_child_int("width");
    this->resp_latency = this->get_js_config()->get_child_int("resp_latency");
    this->beat_allocator = vp::IoReqAllocator::get(this->width);
}

void ObiCut::reset(bool active)
{
    if (!active)
    {
        return;
    }
    for (int ch = 0; ch < NB_CHANNELS; ch++)
    {
        this->req_queue[ch].clear();
        this->resp_queue[ch].clear();
        this->down_denied[ch] = false;
        this->up_denied[ch] = false;
        this->in_denied[ch] = false;
    }
    this->pending.clear();
}

vp::IoReqStatus ObiCut::in_req(vp::Block *__this, vp::IoReq *req)
{
    ObiCut *_this = (ObiCut *)__this;
    int ch = channel_of(req);

    if (_this->req_queue[ch].size() >= OBI_CUT_DEPTH)
    {
        _this->in_denied[ch] = true;
        return vp::IO_REQ_DENIED;
    }

    if (!req->is_first || !req->is_last || req->get_size() > (uint64_t)_this->width)
    {
        _this->trace.force_warning("Unsupported access, only single beats of up to %d bytes "
            "(addr: 0x%lx, size: 0x%lx, first: %d, last: %d)\n", _this->width,
            req->get_addr(), req->get_size(), req->is_first, req->is_last);
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    vp::IoReq *down = req;
    if (req->get_opcode() == vp::READ)
    {
        // The read request belongs to the master and carries no data: the
        // slave fills a response beat, which then goes back upstream
        down = _this->beat_allocator->alloc();
        down->prepare();
        down->set_addr(req->get_addr());
        down->set_size(req->get_size());
        down->set_opcode(vp::READ);
        down->is_first = true;
        down->is_last = true;
        down->burst_id = req->burst_id;
        down->initiator = req->initiator;
    }

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Taking request (addr: 0x%lx, size: 0x%lx, "
        "opcode: %d)\n", req->get_addr(), req->get_size(), req->get_opcode());

    _this->req_queue[ch].push_back(Access{req, down, ch, _this->clock.get_cycles() + 1});
    _this->schedule();
    return vp::IO_REQ_GRANTED;
}

void ObiCut::send_down(int ch)
{
    std::deque<Access> &queue = this->req_queue[ch];
    if (queue.empty() || this->down_denied[ch] || queue.front().ready > this->clock.get_cycles())
    {
        return;
    }

    Access access = queue.front();
    vp::IoReq *down = access.down;
    // Any latency annotated by the slave on an inline answer is ours to apply
    down->inc_latency(-down->get_latency());

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Forwarding request (addr: 0x%lx)\n",
        down->get_addr());

    vp::IoReqStatus status = this->out.req(down);
    if (status == vp::IO_REQ_DENIED)
    {
        this->down_denied[ch] = true;
        return;
    }

    queue.pop_front();
    if (this->in_denied[ch])
    {
        this->in_denied[ch] = false;
        this->in.retry(ch == CH_WRITE ? vp::IO_RETRY_WRITE : vp::IO_RETRY_READ);
    }

    if (status == vp::IO_REQ_DONE)
    {
        this->respond(access, down->get_full_latency());
    }
    else
    {
        this->pending[down] = access;
    }
}

void ObiCut::respond(Access &access, int64_t latency)
{
    vp::IoReq *down = access.down;
    vp::IoReq *resp = down;

    if (access.up->get_opcode() == vp::WRITE)
    {
        // The write beat was granted to us: the master gets a separate ack
        vp::IoRespStatus status = down->get_resp_status();
        uint64_t addr = down->get_addr();
        uint64_t size = down->get_size();
        resp = vp::io_v2_write_ack(down);
        resp->set_addr(addr);
        resp->set_size(size);
        resp->set_resp_status(status);
    }

    // Spill register of the R channel, behind the slave
    int64_t ready = this->clock.get_cycles() + 1 + this->resp_latency + latency;
    this->resp_queue[access.channel].push_back(Access{access.up, resp, access.channel, ready});
    this->schedule();
}

vp::IoRespAck ObiCut::out_resp(vp::Block *__this, vp::IoReq *req)
{
    ObiCut *_this = (ObiCut *)__this;
    auto it = _this->pending.find(req);
    if (it == _this->pending.end())
    {
        _this->trace.fatal("Response for an unknown request (req: %p)\n", req);
        return vp::IO_RESP_ACCEPTED;
    }
    Access access = it->second;
    _this->pending.erase(it);
    _this->respond(access, 0);
    return vp::IO_RESP_ACCEPTED;
}

void ObiCut::send_up(int ch)
{
    std::deque<Access> &queue = this->resp_queue[ch];
    if (queue.empty() || this->up_denied[ch] || queue.front().ready > this->clock.get_cycles())
    {
        return;
    }

    vp::IoReq *resp = queue.front().down;
    // The timing is carried by the cycle of the response
    resp->inc_latency(-resp->get_latency());

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Sending response (addr: 0x%lx)\n",
        resp->get_addr());

    if (this->in.resp(resp) == vp::IO_RESP_DENIED)
    {
        this->up_denied[ch] = true;
        return;
    }
    queue.pop_front();
}

void ObiCut::out_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
    ObiCut *_this = (ObiCut *)__this;
    // The held requests are sent again from here, in the same cycle
    for (int ch = 0; ch < NB_CHANNELS; ch++)
    {
        if (channel == vp::IO_RETRY_ANY || (int)channel == ch)
        {
            if (_this->down_denied[ch])
            {
                _this->down_denied[ch] = false;
                _this->send_down(ch);
            }
        }
    }
    _this->schedule();
}

void ObiCut::in_resp_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
    ObiCut *_this = (ObiCut *)__this;
    for (int ch = 0; ch < NB_CHANNELS; ch++)
    {
        if (channel == vp::IO_RETRY_ANY || (int)channel == ch)
        {
            if (_this->up_denied[ch])
            {
                _this->up_denied[ch] = false;
                _this->send_up(ch);
            }
        }
    }
    _this->schedule();
}

void ObiCut::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ObiCut *_this = (ObiCut *)__this;
    for (int ch = 0; ch < NB_CHANNELS; ch++)
    {
        // Responses first, so that an inline answer of this cycle waits for
        // the next one, as behind the R spill register
        _this->send_up(ch);
        _this->send_down(ch);
    }
    _this->schedule();
}

void ObiCut::schedule()
{
    int64_t now = this->clock.get_cycles();
    int64_t next = -1;
    for (int ch = 0; ch < NB_CHANNELS; ch++)
    {
        if (!this->req_queue[ch].empty() && !this->down_denied[ch])
        {
            int64_t ready = this->req_queue[ch].front().ready;
            if (next == -1 || ready < next) next = ready;
        }
        if (!this->resp_queue[ch].empty() && !this->up_denied[ch])
        {
            int64_t ready = this->resp_queue[ch].front().ready;
            if (next == -1 || ready < next) next = ready;
        }
    }
    if (next == -1)
    {
        return;
    }
    int64_t delay = next > now ? next - now : 1;
    if (this->fsm_event->is_enqueued())
    {
        if (this->fsm_event->get_cycle() <= now + delay)
        {
            return;
        }
        this->event_cancel(this->fsm_event);
    }
    this->event_enqueue(this->fsm_event, delay);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ObiCut(config);
}
