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

#include "idma_axi_write.hpp"



IdmaAxiWrite::IdmaAxiWrite(vp::Component *top, std::string itf_name, IdmaBackend *be, int width,
    int burst_len, int num_ax_in_flight, int meta_fifo_depth, bool raw_coupling)
:   vp::Block(top, itf_name),
    be(be),
    bus(&IdmaAxiWrite::retry_meth, &IdmaAxiWrite::resp_meth),
    width(width),
    num_ax_in_flight(num_ax_in_flight),
    raw_coupling(raw_coupling)
{
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

    // A burst lives from its AW to its B: at most meta_fifo_depth of them
    // (the w_last FIFO bounds them), plus one for same-cycle recycling
    int nb_ctxs = (meta_fifo_depth > 0 ? meta_fifo_depth : num_ax_in_flight + 3) + 1;
    this->ctxs.resize(nb_ctxs);
    for (int i = 0; i < nb_ctxs; i++)
    {
        this->ctxs[i].slot = i;
        this->ctxs[i].stage.resize(this->page_size + width);
    }
}



void IdmaAxiWrite::reset(bool active)
{
    if (active)
    {
        this->free_ctxs.clear();
        for (WriteCtx &ctx: this->ctxs)
        {
            ctx.in_use = false;
            this->free_ctxs.push_back(&ctx);
        }
        this->data_queue.clear();
        this->aw_waiting.clear();
        this->aw_credits = 0;
        if (this->held_beat != nullptr)
        {
            this->held_beat->free();
            this->held_beat = nullptr;
        }
        this->held_ctx = nullptr;
    }
}



uint32_t IdmaAxiWrite::bytes_to_page_boundary(uint64_t addr)
{
    return this->page_size - (addr & (this->page_size - 1));
}



// AW channel readiness as the legalizer sees it. io_v2 has no separate
// address phase (a write burst is its beats), so without the coupler an AW
// is never refused; with the coupler (idma_channel_coupler) the AW store of
// depth num_ax_in_flight holds the addresses waiting for a read credit.
bool IdmaAxiWrite::aw_ready()
{
    if (this->free_ctxs.empty())
    {
        return false;
    }
    // With the coupler the AW store holds num_ax_in_flight addresses waiting
    // for their read credit; without it the fall-through register never
    // blocks since io_v2 has no address phase to be refused
    if (this->raw_coupling)
    {
        return (int)this->aw_waiting.size() < this->num_ax_in_flight;
    }
    return true;
}



void IdmaAxiWrite::issue_aw(const IdmaSplit &split)
{
    WriteCtx *ctx = this->free_ctxs.back();
    this->free_ctxs.pop_back();
    ctx->in_use = true;
    ctx->beats_sent = 0;
    ctx->num_beats = split.num_beats;
    ctx->aw_issued = true;
    ctx->error = false;

    if (this->raw_coupling && !split.decouple_aw)
    {
        // The AW leaves when a read burst delivered its first beat
        if (this->aw_credits > 0)
        {
            this->aw_credits--;
        }
        else
        {
            ctx->aw_issued = false;
            this->aw_waiting.push_back(ctx);
        }
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Issuing write burst (slot: %d, addr: 0x%lx, beats: %d, aw_issued: %d)\n",
        ctx->slot, split.addr, split.num_beats, ctx->aw_issued);

    this->data_queue.push_back(ctx);
}



// Coupler credit: the first response beat of a read burst releases one
// waiting AW, or is remembered for the next one (the RTL aw_to_send counter).
// The back-end calls this for every read burst not flagged decouple_aw.
void IdmaAxiWrite::on_read_first_beat()
{
    if (!this->raw_coupling)
    {
        return;
    }

    if (!this->aw_waiting.empty())
    {
        WriteCtx *ctx = this->aw_waiting.front();
        this->aw_waiting.pop_front();
        ctx->aw_issued = true;
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Coupled AW released (slot: %d)\n", ctx->slot);
        // The legalizer may have been waiting for the AW store
        this->be->wake();
    }
    else
    {
        this->aw_credits++;
    }
}



// W channel handshake. GRANTED: the target owns the beat (and frees it, or
// answers the burst later with one data-less ack). DENIED: w_ready low, the
// beat is held and re-sent from retry_meth. DONE: only legal on the last
// beat (inline burst ack) or as an inline error on any beat.
bool IdmaAxiWrite::send_beat(vp::IoReq *beat)
{
    WriteCtx *ctx = (WriteCtx *)beat->initiator;
    bool is_last = beat->is_last;

    vp::IoReqStatus status = this->bus.req(beat);

    if (status == vp::IO_REQ_DENIED)
    {
        this->held_beat = beat;
        this->held_ctx = ctx;
        return false;
    }

    if (status == vp::IO_REQ_DONE)
    {
        // Inline burst ack on the last beat; an inline answer on another beat
        // is the target refusing it as invalid: keep sending the rest of the
        // burst so the pipeline drains, and report the error on the ack
        vp::IoRespStatus resp_status = beat->get_resp_status();
        if (!is_last && resp_status != vp::IO_RESP_INVALID)
        {
            this->trace.fatal("Non-last write beat answered inline (slot: %d, addr: 0x%lx)\n",
                ctx->slot, beat->get_addr());
        }
        if (resp_status == vp::IO_RESP_INVALID)
        {
            this->trace.force_warning("Invalid access during write beat (slot: %d, "
                "addr: 0x%lx)\n", ctx->slot, beat->get_addr());
            ctx->error = true;
        }
        beat->free();
        if (is_last)
        {
            this->complete(ctx, ctx->error ? vp::IO_RESP_INVALID : vp::IO_RESP_OK);
        }
    }

    return true;
}



// One data beat per cycle at most (idma_axi_write ready_to_write): the
// burst heading the data queue must have its AW issued, and the buffer
// must hold every byte the beat strobes. The beat covers the contiguous run
// of strobed bytes, its data aliasing the per-burst staging buffer, which
// lives until the burst is acknowledged.
bool IdmaAxiWrite::tick(int64_t now)
{
    if (this->held_beat != nullptr || this->data_queue.empty())
    {
        return false;
    }

    WriteCtx *ctx = this->data_queue.front();
    if (!ctx->aw_issued)
    {
        return false;
    }

    IdmaSplit *split;
    int beat_idx;
    uint64_t mask;
    if (!this->be->write_beat_ready(this, &split, &beat_idx, &mask, now))
    {
        return false;
    }

    // The split lives in the back-end FIFO and goes away with its last beat
    uint64_t split_addr = split->addr;
    bool is_last = beat_idx == split->num_beats - 1;

    uint8_t *bus_word = ctx->stage.data() + beat_idx * this->width;
    this->be->write_beat_take(this, bus_word, now);

    int first = __builtin_ctzll(mask);
    int count = __builtin_popcountll(mask);

    vp::IoReq *beat = this->req_allocator->alloc();
    beat->prepare();
    beat->set_is_write(true);
    beat->set_addr((split_addr & ~(uint64_t)(this->width - 1)) + beat_idx * this->width + first);
    beat->set_size(count);
    beat->set_data(bus_word + first);
    beat->is_first = beat_idx == 0;
    beat->is_last = is_last;
    beat->burst_id = ctx->slot;
    beat->initiator = ctx;
    beat->set_resp_status(vp::IO_RESP_OK);

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Sending write beat (slot: %d, addr: 0x%lx, size: %d, first: %d, last: %d)\n",
        ctx->slot, beat->get_addr(), count, beat->is_first, is_last);

    this->traces.declare_access(beat->get_addr(), count, true);

    ctx->beats_sent++;
    if (is_last)
    {
        this->data_queue.pop_front();
    }

    this->send_beat(beat);

    return true;
}



void IdmaAxiWrite::retry_meth(vp::Block *__this, vp::IoRetryChannel channel)
{
    IdmaAxiWrite *_this = (IdmaAxiWrite *)__this;

    if (_this->held_beat == nullptr
        || (channel != vp::IO_RETRY_ANY && channel != vp::IO_RETRY_WRITE))
    {
        return;
    }

    vp::IoReq *beat = _this->held_beat;
    _this->held_beat = nullptr;
    _this->held_ctx = nullptr;

    if (_this->send_beat(beat))
    {
        // Next beat next cycle
        _this->be->wake();
    }
}



vp::IoRespAck IdmaAxiWrite::resp_meth(vp::Block *__this, vp::IoReq *ack)
{
    IdmaAxiWrite *_this = (IdmaAxiWrite *)__this;
    WriteCtx *ctx = (WriteCtx *)ack->initiator;

    if (!ack->is_last || ack->get_data() != NULL)
    {
        _this->trace.fatal("Malformed write burst ack (slot: %d, last: %d, data: %p)\n",
            ctx->slot, ack->is_last, ack->get_data());
    }

    vp::IoRespStatus status = ack->get_resp_status();
    ack->free();
    _this->complete(ctx, status);

    return vp::IO_RESP_ACCEPTED;
}



void IdmaAxiWrite::complete(WriteCtx *ctx, vp::IoRespStatus status)
{
    if (status == vp::IO_RESP_INVALID)
    {
        this->trace.force_warning("Write burst failed (slot: %d)\n", ctx->slot);
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Write burst acknowledged (slot: %d)\n", ctx->slot);

    ctx->in_use = false;
    this->free_ctxs.push_back(ctx);

    this->be->write_burst_done(this, status == vp::IO_RESP_INVALID);
}



bool IdmaAxiWrite::busy()
{
    return this->free_ctxs.size() != this->ctxs.size();
}
