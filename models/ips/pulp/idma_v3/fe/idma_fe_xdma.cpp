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

#include <vp/vp.hpp>
#include "idma_fe_xdma.hpp"
#include "../be/idma_be.hpp"

// Ids start at 2 as in the register front-end
#define FIRST_ID 2



IdmaFeXdma::IdmaFeXdma(vp::Component *top, IdmaFeSink *me)
:   Block(top, "fe"),
    me(me),
    done_event(this, &IdmaFeXdma::done_handler),
    src(*this, "src", 64),
    dst(*this, "dst", 64),
    src_stride(*this, "src_stride", 32),
    dst_stride(*this, "dst_stride", 32),
    reps(*this, "reps", 32),
    next_transfer_id(*this, "next_transfer_id", 32, true, FIRST_ID),
    completed_id(*this, "completed_id", 32, true, FIRST_ID - 1),
    trace_busy(*this, "busy", 1, vp::SignalCommon::ResetKind::HighZ)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->offload_itf.set_sync_meth(&IdmaFeXdma::offload_sync);
    top->new_slave_port("offload", &this->offload_itf, this);
    top->new_master_port("offload_grant", &this->offload_grant_itf, this);
    top->new_master_port("irq", &this->irq_itf, this);
    top->new_master_port("busy", &this->busy_itf, this);
}



void IdmaFeXdma::reset(bool active)
{
    if (active)
    {
        if (this->stalled != nullptr)
        {
            delete this->stalled;
            this->stalled = nullptr;
        }
        this->pending_done = 0;
        this->busy = false;
        this->grant_replay = false;
    }
}



void IdmaFeXdma::offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    IdmaFeXdma *_this = (IdmaFeXdma *)__this;
    uint32_t func7 = insn->opcode >> 25;

    insn->granted = true;

    switch (func7)
    {
        case 0b0000000:
        {
            uint64_t addr = (((uint64_t)insn->arg_b) << 32) | insn->arg_a;
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmsrc (addr: 0x%lx)\n", addr);
            _this->src.set(addr);
            break;
        }
        case 0b0000001:
        {
            uint64_t addr = (((uint64_t)insn->arg_b) << 32) | insn->arg_a;
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmdst (addr: 0x%lx)\n", addr);
            _this->dst.set(addr);
            break;
        }
        case 0b0000110:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmstr (src_stride: 0x%x, "
                "dst_stride: 0x%x)\n", insn->arg_a, insn->arg_b);
            _this->src_stride.set(insn->arg_a);
            _this->dst_stride.set(insn->arg_b);
            break;
        case 0b0000111:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmrep (reps: 0x%x)\n", insn->arg_a);
            _this->reps.set(insn->arg_a);
            break;
        case 0b0000011:
        case 0b0000010:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmcpy (config: 0x%x, size: 0x%x)\n",
                insn->arg_b, insn->arg_a);
            insn->result = _this->enqueue_copy(insn->arg_b, insn->arg_a, insn->granted);
            break;
        case 0b0000101:
        case 0b0000100:
            insn->result = _this->get_status(insn->arg_b);
            break;
    }
}



uint32_t IdmaFeXdma::get_status(uint32_t status)
{
    switch (status)
    {
        case 0: return this->completed_id.get();
        case 1: return this->next_transfer_id.get() + 1;
        case 2: return this->next_transfer_id.get() - this->completed_id.get() != 1;
        case 3: return !this->me->can_accept_nd();
    }
    return 0;
}



// dmcpy / dmcpyi. Three situations, all decided inside the core's call:
// - a transfer is stalled (FIFO was full): the core is replaying the
//   instruction, refuse again;
// - the grant just went back: this is the replay that completes the
//   instruction, answer with the id already allocated, nothing to enqueue;
// - otherwise allocate the id and push, or stall if the FIFO is full.
uint32_t IdmaFeXdma::enqueue_copy(uint32_t config, uint32_t size, bool &granted)
{
    if (this->stalled != nullptr)
    {
        // The core replays the instruction while its grant is refused:
        // nothing new to enqueue until the grant went back
        granted = false;
        return this->stalled->id;
    }

    if (this->grant_replay)
    {
        // The replay following the grant: the transfer is already queued
        this->grant_replay = false;
        granted = true;
        return this->grant_id;
    }

    uint32_t transfer_id = this->next_transfer_id.get();
    uint32_t next = transfer_id + 1;
    if (next < FIRST_ID) next = FIRST_ID;
    this->next_transfer_id.set(next);

    IdmaNdReq *req = new IdmaNdReq();
    req->src = this->src.get();
    req->dst = this->dst.get();
    req->length = size;
    req->src_stride[0] = this->src_stride.get();
    req->dst_stride[0] = this->dst_stride.get();
    req->reps[0] = this->reps.get();
    req->nd = (config >> 1) & 1;
    req->decouple_rw = config & 1;
    req->src_prot = IDMA_PROT_AXI;
    req->dst_prot = IDMA_PROT_AXI;
    req->id = transfer_id;
    req->stream = 0;

    this->trace.msg(vp::Trace::LEVEL_INFO, "Enqueuing transfer (id: %d, src: 0x%lx, dst: 0x%lx, "
        "size: 0x%lx, src_stride: 0x%lx, dst_stride: 0x%lx, reps: %ld, config: 0x%x)\n",
        transfer_id, req->src, req->dst, req->length, req->src_stride[0], req->dst_stride[0],
        req->reps[0], config);

    this->trace_busy = true;

    if (this->me->can_accept_nd())
    {
        granted = true;
        this->me->push_nd(req, 0);
    }
    else
    {
        // The request FIFO is full: stall the core until a slot frees
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Request FIFO full, stalling the core\n");
        this->stalled = req;
        granted = false;
    }

    this->update_busy();

    return transfer_id;
}



void IdmaFeXdma::me_ready()
{
    if (this->stalled != nullptr && this->me->can_accept_nd())
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Request FIFO got ready, unblocking the core\n");

        IdmaNdReq *req = this->stalled;
        this->stalled = nullptr;
        this->grant_replay = true;
        this->grant_id = req->id;

        this->me->push_nd(req, 0);
        IssOffloadInsnGrant<uint32_t> offload_grant = { .result = req->id };
        this->offload_grant_itf.sync(&offload_grant);
    }
}



void IdmaFeXdma::complete_nd(IdmaNdReq *req)
{
    this->trace.msg(vp::Trace::LEVEL_INFO, "Transfer done (id: %d)\n", req->id);
    this->pending_done++;
    this->done_event.enqueue(1);
}



void IdmaFeXdma::done_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IdmaFeXdma *_this = (IdmaFeXdma *)__this;

    while (_this->pending_done > 0)
    {
        _this->pending_done--;
        uint32_t next = _this->completed_id.get() + 1;
        if (next < FIRST_ID) next = FIRST_ID;
        _this->completed_id.set(next);
    }

    if (_this->completed_id.get() + 1 == _this->next_transfer_id.get())
    {
        _this->trace_busy = false;
    }

    if (_this->irq_itf.is_bound())
    {
        _this->irq_itf.sync(true);
    }

    _this->update_busy();
}



void IdmaFeXdma::update_busy()
{
    bool busy = this->me->is_busy() || (this->be != nullptr && this->be->is_busy())
        || this->stalled != nullptr;
    if (busy != this->busy)
    {
        this->busy = busy;
        if (this->busy_itf.is_bound())
        {
            this->busy_itf.sync(busy);
        }
    }
}
