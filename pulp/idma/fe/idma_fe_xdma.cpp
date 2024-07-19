/*
 * Copyright (C) 2024 ETH Zurich and University of Bologna
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
 * Authors: Germain Haugou, ETH Zurich (germain.haugou@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include "idma_fe_xdma.hpp"



IDmaFeXdma::IDmaFeXdma(vp::Component *idma, IdmaTransferConsumer *me)
    : Block(idma, "fe"),
    src(*this, "src", 64),
    dst(*this, "dst", 64),
    src_stride(*this, "src_stride", 32),
    dst_stride(*this, "dst_stride", 32),
    reps(*this, "reps", 32),
    next_transfer_id(*this, "next_transfer_id", 32, true, 2),
    completed_id(*this, "completed_id", 32, true, 1),
    do_transfer_grant(*this, "do_transfer_grant", 1)
{
    // Middle-end will be used later for interaction
    this->me = me;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Declare offload slave interface where instructions will be offloaded
    this->offload_itf.set_sync_meth(&IDmaFeXdma::offload_sync);
    idma->new_slave_port("offload", &this->offload_itf, this);

    // Declare offload master interface for granting blocked transfers
    idma->new_master_port("offload_grant", &this->offload_grant_itf, this);
}



// Called by core to offload xdma instructions
void IDmaFeXdma::offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    IDmaFeXdma *_this = (IDmaFeXdma *)__this;
    uint32_t func7 = insn->opcode >> 25;

    insn->granted = true;

    switch (func7)
    {
        case 0b0000000:
        {
            uint64_t addr = (((uint64_t)insn->arg_b) << 32) | insn->arg_a;
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmsrc operation (addr: 0x%llx)\n",
                addr);
            _this->src.set(addr);
            break;
        }
        case 0b0000001:
        {
            uint64_t addr = (((uint64_t)insn->arg_b) << 32) | insn->arg_a;
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmdst operation (addr: 0x%llx)\n",
                addr);
            _this->dst.set(addr);
            break;
        }
        case 0b0000110:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmstr operation (src_stride: 0x%lx, dst_stride: 0x%lx)\n",
                insn->arg_a, insn->arg_b);
            _this->src_stride.set(insn->arg_a);
            _this->dst_stride.set(insn->arg_b);
            break;
        case 0b0000111:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmrep operation (reps: 0x%lx)\n", insn->arg_a);
            _this->reps.set(insn->arg_a);
            break;
        case 0b0000011:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmcpy operation (config: 0x%lx, size: 0x%lx)\n",
                insn->arg_b, insn->arg_a);
            insn->result = _this->enqueue_copy(insn->arg_b, insn->arg_a, insn->granted);
            break;
        case 0b0000101:
            // _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmstat operation (status: 0x%lx)\n",
            //     insn->arg_b);
            insn->result = _this->get_status(insn->arg_b);
            break;
        case 0b0000010:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmcpy operation (config: 0x%lx, size: 0x%lx)\n",
                insn->arg_b, insn->arg_a);
            insn->result = _this->enqueue_copy(insn->arg_b, insn->arg_a, insn->granted);
            break;
        case 0b0000100:
            // _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dmstat operation (status: 0x%lx)\n",
            //     insn->arg_b);
            insn->result = _this->get_status(insn->arg_b);
            break;
    }
}



uint32_t IDmaFeXdma::get_status(uint32_t status)
{
    switch (status)
    {
        case 0: return this->completed_id.get();
        case 1: return this->next_transfer_id.get() + 1;
        case 2: return this->next_transfer_id.get() - this->completed_id.get() != 1;
        case 3: return !this->me->can_accept_transfer();
    }

    return 0;
}



uint32_t IDmaFeXdma::enqueue_copy(uint32_t config, uint32_t size, bool &granted)
{
    // Allocate transfer ID
    uint32_t transfer_id = this->next_transfer_id.get();
    this->next_transfer_id.set(transfer_id + 1);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Allocated transfer ID (id: %d)\n", transfer_id);

    // Allocate a new transfer and fill it from registers
    IdmaTransfer *transfer = new IdmaTransfer();
    transfer->src = this->src.get();
    transfer->dst = this->dst.get();
    transfer->size = size;
    transfer->src_stride = this->src_stride.get();
    transfer->dst_stride = this->dst_stride.get();
    transfer->reps = this->reps.get();
    transfer->config = config;

    // In case size is 0, directly terminate the transfer. This could be done few cycles
    // after to better match HW.
    if (size == 0)
    {
        this->ack_transfer(transfer);
        return transfer_id;
    }

    // Check if middle end can accept a new transfer
    if (this->me->can_accept_transfer())
    {
        // If no enqueue the burst
        granted = true;
        this->me->enqueue_transfer(transfer);
    }
    else
    {
        // Otherwise, stall the core and keep the transfer until we can grant it
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Middle-end not ready, blocking transfer\n");
        this->stalled_transfer = transfer;
        granted = false;

        this->do_transfer_grant.set(true);
    }

    return transfer_id;
}



// Called by middle-end when a transfer is done
void IDmaFeXdma::ack_transfer(IdmaTransfer *transfer)
{
    this->completed_id.inc(1);
    delete transfer;
}



// Called by middle-end when something has been updated to check if we must take any action
void IDmaFeXdma::update()
{
    // In case a transfer is blocked and the middle-end is now ready, unblock it and grant it
    // to unstall the core
    if (this->do_transfer_grant.get() && this->me->can_accept_transfer())
    {
        this->do_transfer_grant.set(false);

        this->trace.msg(vp::Trace::LEVEL_TRACE, "Middle-end got ready, unblocking transfer\n");

        IdmaTransfer *transfer = this->stalled_transfer;

        IssOffloadInsnGrant<uint32_t> offload_grant = {
            .result=this->next_transfer_id.get() - 1
        };
        this->offload_grant_itf.sync(&offload_grant);
        this->me->enqueue_transfer(transfer);
    }
}



void IDmaFeXdma::reset(bool active)
{
}