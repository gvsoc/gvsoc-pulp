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
#include "idma_legalizer.hpp"
#include "idma_be.hpp"



IdmaLegalizer::IdmaLegalizer(IdmaBackend *be, int width)
:   be(be), width(width)
{
    this->log2_width = 0;
    while ((1 << this->log2_width) < width)
    {
        this->log2_width++;
    }
}



void IdmaLegalizer::reset()
{
    this->r = Side();
    this->w = Side();
    this->req = nullptr;
    this->rm = nullptr;
    this->wm = nullptr;
    this->coupled = false;
    this->last_take_cycle = -1;
}



// Descriptor of one burst of one side (the RTL r_dp_req / w_dp_req fields):
// offset and tailer bound the valid bytes of the first and last beat,
// num_beats is the AXI AxLEN + 1 of the word-aligned burst.
IdmaSplit IdmaLegalizer::make_split(const Side &side, uint32_t num_bytes)
{
    IdmaSplit split;
    split.addr = side.addr;
    split.num_bytes = num_bytes;
    split.offset = side.addr & (this->width - 1);
    split.tailer = (num_bytes + split.offset) & (this->width - 1);
    split.shift = side.shift;
    split.num_beats = ((num_bytes + split.offset - 1) >> this->log2_width) + 1;
    split.is_single = split.num_beats == 1;
    split.last = side.length == num_bytes;
    split.super_last = split.last && this->req->super_last;
    split.decouple_aw = this->req->decouple_aw;
    split.req = this->req;
    return split;
}



bool IdmaLegalizer::take_request(int64_t now)
{
    // The mid-end hands over at most one 1D request per cycle
    if (this->last_take_cycle == now)
    {
        return false;
    }

    Idma1dReq *req = this->be->me->peek_1d(now);
    if (req == nullptr)
    {
        return false;
    }

    this->last_take_cycle = now;
    this->be->me->take_1d(now);

    if (req->length == 0)
    {
        // Zero-length requests are rejected by the back-end: answered in the
        // same cycle with an error response flagged last, without going
        // through the legalizer (idma_backend RejectZeroTransfers).
        this->be->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Rejecting zero-length request (src: 0x%lx, dst: 0x%lx)\n", req->src, req->dst);
        this->be->me->complete_1d(req, true);
        return true;
    }

    IdmaReadManager *rm = this->be->read_manager(req->src_prot);
    IdmaWriteManager *wm = this->be->write_manager(req->dst_prot);
    if (rm == nullptr || wm == nullptr)
    {
        this->be->trace.fatal("Unsupported protocol pair on this stream "
            "(src_prot: %d, dst_prot: %d, src: 0x%lx, dst: 0x%lx)\n",
            req->src_prot, req->dst_prot, req->src, req->dst);
        return true;
    }

    this->req = req;
    this->rm = rm;
    this->wm = wm;

    this->r.valid = true;
    this->r.addr = req->src;
    this->r.length = req->length;
    this->r.shift = req->src & (this->width - 1);

    // read_shift = src % width and write_shift = -(dst % width): with these
    // the byte at transfer offset n always sits in lane (n % width) of the
    // buffer, on both sides, whatever the two alignments
    this->w.valid = true;
    this->w.addr = req->dst;
    this->w.length = req->length;
    this->w.shift = (this->width - (req->dst & (this->width - 1))) & (this->width - 1);

    // The read and write machines run in lock-step unless software decoupled
    // them or one side does not burst (OBI / INIT), in which case the RTL
    // lets each side advance on its own.
    this->coupled = !req->decouple_rw && !rm->not_bursting() && !wm->not_bursting();

    this->be->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Taking request (src: 0x%lx, dst: 0x%lx, length: 0x%lx, src_prot: %d, dst_prot: %d, "
        "coupled: %d, super_last: %d)\n", req->src, req->dst, req->length, req->src_prot,
        req->dst_prot, this->coupled, req->super_last);

    return true;
}



bool IdmaLegalizer::step(int64_t now)
{
    bool active = false;

    if (!this->r.valid && !this->w.valid)
    {
        // Idle: a request taken now produces its first split next cycle
        return this->take_request(now);
    }

    // A zero-length request queued behind the current one bypasses the
    // legalizer altogether in the RTL: it is rejected right away.
    Idma1dReq *next = this->be->me->peek_1d(now);
    if (next != nullptr && next->length == 0 && this->last_take_cycle != now)
    {
        this->last_take_cycle = now;
        this->be->me->take_1d(now);
        this->be->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Rejecting zero-length request (src: 0x%lx, dst: 0x%lx)\n", next->src, next->dst);
        this->be->me->complete_1d(next, true);
        active = true;
    }

    // The RTL flow control: r_ready = r_dp_req_in_ready & ar_ready,
    // w_ready = w_dp_req_in_ready & aw_ready & w_last_ready
    bool r_ready = this->r.valid && this->be->r_fifo.can_push(now) && this->rm->ar_ready();
    bool w_ready = this->w.valid && this->be->w_fifo.can_push(now)
        && this->be->wlast.can_push(now) && this->wm->aw_ready();

    // Each side may go up to the next boundary its protocol imposes (the RTL
    // page splitter: 2^(offset + burst_len) for AXI, one word when not
    // bursting), and no further than the end of the transfer
    uint64_t r_bytes = 0;
    uint64_t w_bytes = 0;
    if (this->r.valid)
    {
        r_bytes = std::min<uint64_t>(this->r.length, this->rm->bytes_to_page_boundary(this->r.addr));
    }
    if (this->w.valid)
    {
        w_bytes = std::min<uint64_t>(this->w.length, this->wm->bytes_to_page_boundary(this->w.addr));
    }

    bool r_go, w_go;
    if (this->coupled)
    {
        // Both sides take the shorter burst and advance only together
        uint64_t bytes = std::min(r_bytes, w_bytes);
        r_bytes = bytes;
        w_bytes = bytes;
        r_go = w_go = r_ready && w_ready;
    }
    else
    {
        r_go = r_ready;
        w_go = w_ready;
    }

    if (r_go)
    {
        IdmaSplit split = this->make_split(this->r, r_bytes);
        this->be->trace.msg(vp::Trace::LEVEL_TRACE,
            "Read split (addr: 0x%lx, size: 0x%x, beats: %d, offset: %d, tailer: %d, shift: %d)\n",
            split.addr, split.num_bytes, split.num_beats, split.offset, split.tailer, split.shift);
        void *token = this->rm->issue_ar(split);
        this->be->r_fifo.push({split, this->rm, token}, now);
        this->r.addr += r_bytes;
        this->r.length -= r_bytes;
        this->r.valid = this->r.length != 0;
        active = true;
    }

    if (w_go)
    {
        IdmaSplit split = this->make_split(this->w, w_bytes);
        this->be->trace.msg(vp::Trace::LEVEL_TRACE,
            "Write split (addr: 0x%lx, size: 0x%x, beats: %d, offset: %d, tailer: %d, shift: %d, "
            "last: %d)\n", split.addr, split.num_bytes, split.num_beats, split.offset,
            split.tailer, split.shift, split.last);
        this->be->w_fifo.push({split, this->wm}, now);
        this->be->wlast.push({this->req, split.last}, now);
        this->wm->issue_aw(split);
        this->w.addr += w_bytes;
        this->w.length -= w_bytes;
        this->w.valid = this->w.length != 0;
        active = true;
    }

    if (!this->r.valid && !this->w.valid)
    {
        // Last split: the next request is taken in the same cycle
        this->req = nullptr;
        this->take_request(now);
    }

    return active;
}
