// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Asynchronous SRAM for the ri5ky testbench. Functionally a byte-addressable
// backing store like memory_v3, but it answers every request with
// IO_REQ_GRANTED and completes it asynchronously `latency` cycles later via
// in.resp(). This mirrors the RTL gv_tb/slow_mem.sv (gnt the request cycle,
// rvalid LATENCY cycles later) and, unlike the synchronous memory_v3, it
// engages p.elw's clock-gated park/wake path — which is how a real event
// unit (always an asynchronous responder) behaves.
//
// Reuses MemoryV3Config (size + latency) so no new config struct is needed.

#include <stdlib.h>
#include <string.h>
#include <queue>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <memory/memory_v3/memory_v3_config.hpp>

class Ri5kyAsyncMem : public vp::Component
{
public:
    Ri5kyAsyncMem(vp::ComponentConf &config);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    MemoryV3Config cfg;

private:
    static void resp_handler(vp::Block *__this, vp::ClockEvent *event);
    void reset(bool active) override;

    vp::Trace trace;
    // Async io_v2 slave: replies via in.resp() on our own port.
    vp::IoSlave in{&Ri5kyAsyncMem::req};
    // One ClockEvent paces all completions. Requests are accepted in order
    // with a constant latency, so due cycles are monotonic and a plain FIFO
    // (head = next to complete) is sufficient — no per-request event needed.
    vp::ClockEvent event;
    std::queue<vp::IoReq *> pending;
    std::queue<int64_t> due;

    uint8_t *mem_data;
    uint64_t truncate_mask;
};


Ri5kyAsyncMem::Ri5kyAsyncMem(vp::ComponentConf &config)
    : vp::Component(config, this->cfg),
      event(this, &Ri5kyAsyncMem::resp_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->in);

    this->mem_data = (uint8_t *)calloc(1, this->cfg.size);
    this->truncate_mask = (uint64_t)this->cfg.size - 1;
}


void Ri5kyAsyncMem::reset(bool active)
{
    if (active)
    {
        while (!this->pending.empty()) this->pending.pop();
        while (!this->due.empty()) this->due.pop();
    }
}


vp::IoReqStatus Ri5kyAsyncMem::req(vp::Block *__this, vp::IoReq *req)
{
    Ri5kyAsyncMem *_this = (Ri5kyAsyncMem *)__this;

    uint64_t offset = req->get_addr() & _this->truncate_mask;
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();

    if (offset + size > (uint64_t)_this->cfg.size)
    {
        _this->trace.force_warning("Out-of-bound request (offset: 0x%llx, size: 0x%llx)\n",
            (unsigned long long)offset, (unsigned long long)size);
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    // Perform the access now; the data is delivered to the master only when
    // the deferred resp() fires (reads) — the backing store does not change
    // in between for our single-outstanding traffic.
    if (req->get_opcode() == vp::IoReqOpcode::READ)
    {
        if (data) memcpy((void *)data, (void *)&_this->mem_data[offset], size);
    }
    else if (req->get_opcode() == vp::IoReqOpcode::WRITE)
    {
        if (data) memcpy((void *)&_this->mem_data[offset], (void *)data, size);
    }
    else
    {
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    req->set_resp_status(vp::IO_RESP_OK);

    // Latency 0 degenerates to a synchronous response (matches a 0-cycle
    // RTL memory). Otherwise defer the reply by `latency` cycles.
    if (_this->cfg.latency == 0)
    {
        return vp::IO_REQ_DONE;
    }

    int64_t now = _this->clock.get_cycles();
    _this->pending.push(req);
    _this->due.push(now + (int64_t)_this->cfg.latency);
    if (!_this->event.is_enqueued())
    {
        _this->event.enqueue((int64_t)_this->cfg.latency);
    }

    return vp::IO_REQ_GRANTED;
}


void Ri5kyAsyncMem::resp_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Ri5kyAsyncMem *_this = (Ri5kyAsyncMem *)__this;
    int64_t now = _this->clock.get_cycles();

    // Complete every request whose latency has elapsed this cycle.
    while (!_this->pending.empty() && _this->due.front() <= now)
    {
        vp::IoReq *req = _this->pending.front();
        _this->pending.pop();
        _this->due.pop();
        _this->in.resp(req);
    }

    // Re-arm for the next pending completion.
    if (!_this->pending.empty())
    {
        _this->event.enqueue(_this->due.front() - now);
    }
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Ri5kyAsyncMem(config);
}
