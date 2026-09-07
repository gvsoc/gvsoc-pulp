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

#include <cstring>
#include "idma_axi_read.hpp"



IdmaAxiRead::IdmaAxiRead(vp::Component *top, std::string itf_name, IdmaBackend *be, int width,
    int burst_len, int num_ax_in_flight)
:   vp::Block(top, itf_name),
    be(be),
    bus(&IdmaAxiRead::retry_meth, &IdmaAxiRead::resp_meth),
    width(width)
{
    // The owning component exposes the bus-facing master under itf_name with
    // this block as callback context. The generator declares IoV2Beat on it.
    top->new_master_port(itf_name, &this->bus, this);

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->req_allocator = vp::IoReqAllocator::get(0);

    int log2_width = 0;
    while ((1 << log2_width) < width)
    {
        log2_width++;
    }
    int page_bits = log2_width + burst_len;
    if (page_bits > 12)
    {
        page_bits = 12;
    }
    this->page_size = (uint64_t)1 << page_bits;

    // One context per outstanding burst, plus one so a slot released in the
    // same cycle a split is issued is never needed
    this->ctxs.resize(num_ax_in_flight + 1);
    for (int i = 0; i < (int)this->ctxs.size(); i++)
    {
        this->ctxs[i].slot = i;
    }

    this->bus_word.resize(width);
}



void IdmaAxiRead::start()
{
    if (!this->bus.is_resp_retry_bound())
    {
        this->trace.fatal("idma_v3: the read port needs a producer supporting response "
            "back-pressure (resp_retry); bind it through a beat router or adapter\n");
    }
}



void IdmaAxiRead::reset(bool active)
{
    if (active)
    {
        this->free_ctxs.clear();
        for (ReadCtx &ctx: this->ctxs)
        {
            if (ctx.req != nullptr)
            {
                ctx.req->free();
                ctx.req = nullptr;
            }
            ctx.in_use = false;
            this->free_ctxs.push_back(&ctx);
        }
        if (this->held_ar != nullptr)
        {
            this->held_ar->free();
            this->held_ar = nullptr;
        }
        this->held_resp = nullptr;
    }
}



uint32_t IdmaAxiRead::bytes_to_page_boundary(uint64_t addr)
{
    return this->page_size - (addr & (this->page_size - 1));
}



bool IdmaAxiRead::ar_ready()
{
    return this->held_ar == nullptr && !this->free_ctxs.empty();
}



// The AR channel. In io_v2 a read burst is one data-less request; the data
// comes back as one resp() per bus word, in order, each a distinct pooled
// object the consumer frees. The request itself is ours until the last beat
// (initiator-owned convention), the context pointer rides in `initiator` so
// the beats can be matched to their burst.
void *IdmaAxiRead::issue_ar(const IdmaSplit &split)
{
    ReadCtx *ctx = this->free_ctxs.back();
    this->free_ctxs.pop_back();
    ctx->in_use = true;
    ctx->beats_expected = split.num_beats;
    ctx->beats_received = 0;

    // One data-less request per burst (initiator-owned: freed by us on the
    // last response beat); the data comes back in distinct response beats
    vp::IoReq *req = this->req_allocator->alloc();
    req->prepare();
    req->set_is_write(false);
    req->set_addr(split.addr & ~(uint64_t)(this->width - 1));
    req->set_size((uint64_t)split.num_beats * this->width);
    req->set_data(NULL);
    req->is_first = true;
    req->is_last = true;
    req->burst_id = ctx->slot;
    req->initiator = ctx;
    req->set_resp_status(vp::IO_RESP_OK);
    ctx->req = req;

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Issuing read burst (slot: %d, addr: 0x%lx, beats: %d)\n",
        ctx->slot, req->get_addr(), split.num_beats);

    this->traces.declare_access(split.addr, split.num_bytes, false);

    vp::IoReqStatus status = this->bus.req(req);
    if (status == vp::IO_REQ_DENIED)
    {
        // The address channel stays busy until the bus retries
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Read burst denied (slot: %d)\n", ctx->slot);
        this->held_ar = req;
    }
    else if (status == vp::IO_REQ_DONE)
    {
        this->trace.force_warning("Read burst answered inline (slot: %d, addr: 0x%lx, "
            "status: %d)\n", ctx->slot, req->get_addr(), req->get_resp_status());
        this->release(ctx);
    }

    return ctx;
}



void IdmaAxiRead::retry_meth(vp::Block *__this, vp::IoRetryChannel channel)
{
    IdmaAxiRead *_this = (IdmaAxiRead *)__this;

    if (_this->held_ar == nullptr || (channel != vp::IO_RETRY_ANY && channel != vp::IO_RETRY_READ))
    {
        return;
    }

    vp::IoReq *req = _this->held_ar;
    vp::IoReqStatus status = _this->bus.req(req);
    if (status == vp::IO_REQ_DENIED)
    {
        return;
    }

    _this->held_ar = nullptr;
    if (status == vp::IO_REQ_DONE)
    {
        _this->trace.force_warning("Read burst answered inline on retry (addr: 0x%lx)\n",
            req->get_addr());
        _this->release((ReadCtx *)req->initiator);
    }

    // The legalizer may issue the next split
    _this->be->wake();
}



// Offer one response beat to the back-end. The payload is placed in a
// full-width word at its word offset (a beat is at most one bus word, and
// word-aligned unless an adapter narrowed it), the back-end then takes the
// lanes of the current beat of the split.
bool IdmaAxiRead::push_beat(vp::IoReq *beat)
{
    int64_t now = this->be->cycles();

    if (!this->be->read_beat_can_accept(this, beat->initiator, now))
    {
        return false;
    }

    // Align the beat payload into a full bus word; the back-end takes only
    // the lanes of the current beat
    int offset = beat->get_addr() & (this->width - 1);
    int size = beat->get_size();
    if (offset + size > this->width)
    {
        size = this->width - offset;
    }
    if (size > 0 && beat->get_data() != NULL)
    {
        std::memcpy(this->bus_word.data() + offset, beat->get_data(), size);
    }

    this->be->read_beat_accept(this, beat->initiator, this->bus_word.data(), now);
    return true;
}



// R channel: the RTL accepts a beat when every lane it strobes has room in
// the buffer (r_ready = &(buffer_in_ready | ~mask_in)); otherwise it holds
// r_ready low. The io_v2 equivalent is IO_RESP_DENIED: the producer keeps
// the beat and re-sends it when we call resp_retry() from the back-end tick
// once this cycle's pops are applied.
vp::IoRespAck IdmaAxiRead::resp_meth(vp::Block *__this, vp::IoReq *beat)
{
    IdmaAxiRead *_this = (IdmaAxiRead *)__this;
    ReadCtx *ctx = (ReadCtx *)beat->initiator;

    if (beat->get_resp_status() == vp::IO_RESP_INVALID)
    {
        _this->trace.force_warning("Invalid access during read beat (slot: %d, addr: 0x%lx, "
            "size: 0x%lx)\n", ctx->slot, beat->get_addr(), beat->get_size());
    }

    if (!_this->push_beat(beat))
    {
        // No room in the buffer: the producer keeps the beat until we call
        // resp_retry() from the back-end tick
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Read beat held (slot: %d, addr: 0x%lx)\n",
            ctx->slot, beat->get_addr());
        _this->held_resp = beat;
        _this->be->wake();
        return vp::IO_RESP_DENIED;
    }

    ctx->beats_received++;
    beat->free();

    if (ctx->beats_received == ctx->beats_expected)
    {
        _this->release(ctx);
    }

    return vp::IO_RESP_ACCEPTED;
}



void IdmaAxiRead::retry_held_beat()
{
    if (this->held_resp == nullptr)
    {
        return;
    }

    int64_t now = this->be->cycles();
    if (!this->be->read_beat_can_accept(this, this->held_resp->initiator, now))
    {
        return;
    }

    // The producer re-sends the held beat inside this call; resp_meth then
    // accepts it
    this->held_resp = nullptr;
    this->bus.resp_retry(vp::IO_RETRY_READ);
}



void IdmaAxiRead::release(ReadCtx *ctx)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Read burst done (slot: %d)\n", ctx->slot);
    if (ctx->req != nullptr)
    {
        ctx->req->free();
        ctx->req = nullptr;
    }
    ctx->in_use = false;
    this->free_ctxs.push_back(ctx);
}



bool IdmaAxiRead::busy()
{
    return this->free_ctxs.size() != this->ctxs.size() || this->held_ar != nullptr;
}
