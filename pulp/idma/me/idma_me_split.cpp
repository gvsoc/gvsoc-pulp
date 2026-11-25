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
 * Authors: Marco Bertuletti, ETH Zurich (mbertuletti@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include "idma_me_split.hpp"



IDmaMeSplit::IDmaMeSplit(vp::Component *idma, const IdmaConfig &cfg, std::string name,
    IdmaTransferProducer *fe, IdmaTransferConsumer *be, uint64_t split_size)
:   Block(idma, name),
    fsm_event(this, &IDmaMeSplit::fsm_handler)
{
    // Previous and next stages will be used later for interaction
    this->fe = fe;
    this->be = be;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->split_size = split_size;
    this->loc_base = cfg.loc_base;
    this->loc_size = cfg.loc_size;

    if (split_size == 0 || (split_size & (split_size - 1)) != 0)
    {
        this->trace.fatal("Split size must be a power of 2 (split_size: 0x%lx)\n", split_size);
    }
}



// Called by the previous stage to enqueue a transfer
void IDmaMeSplit::enqueue_transfer(IdmaTransfer *transfer)
{
    if (!this->can_accept_transfer())
    {
        this->trace.fatal("Cannot accept transfer\n");
    }

    if (transfer->collective_op != 0)
    {
        this->trace.fatal("Broadcast and reduction are not supported (transfer: %p, collective_op: %d)\n",
            transfer, transfer->collective_op);
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Queueing transfer (transfer: %p, src: 0x%lx, dst: 0x%lx, size: 0x%lx)\n",
        transfer, transfer->src, transfer->dst, transfer->size);

    this->transfer_queue.push(transfer);

    // The boundaries are taken on the side of the transfer which is in the local area
    bool src_is_loc = transfer->src >= this->loc_base
        && transfer->src < this->loc_base + this->loc_size;
    uint64_t start_addr = src_is_loc ? transfer->src : transfer->dst;
    uint64_t first_size = this->split_size - (start_addr & (this->split_size - 1));

    this->current_transfer = transfer;
    this->current_whole = transfer->size <= first_size;
    this->current_src = transfer->src;
    this->current_dst = transfer->dst;
    this->current_size = transfer->size;
    transfer->nb_bursts = 0;
    transfer->bursts_sent = false;

    this->send_next(true);
}



void IDmaMeSplit::send_next(bool from_enqueue)
{
    if (this->current_transfer == NULL || !this->be->can_accept_transfer())
    {
        return;
    }

    IdmaTransfer *transfer = this->current_transfer;

    if (this->current_whole)
    {
        // Fits in one window, forward it as is
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Forwarding transfer (transfer: %p)\n", transfer);
        this->current_transfer = NULL;
        this->be->enqueue_transfer(transfer);
        if (!from_enqueue)
        {
            this->fe->update();
        }
        return;
    }

    // Cut the next piece, up to the next boundary of the local side
    bool src_is_loc = transfer->src >= this->loc_base
        && transfer->src < this->loc_base + this->loc_size;
    uint64_t addr = src_is_loc ? this->current_src : this->current_dst;
    uint64_t size = this->split_size - (addr & (this->split_size - 1));
    if (size > this->current_size)
    {
        size = this->current_size;
    }

    IdmaTransfer *burst = new IdmaTransfer();
    burst->parent = transfer;
    burst->src = this->current_src;
    burst->dst = this->current_dst;
    burst->size = size;
    burst->reps = 1;
    burst->config = 0;

    this->current_src += size;
    this->current_dst += size;
    this->current_size -= size;
    transfer->nb_bursts++;

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Sending piece (transfer: %p, piece: %p, src: 0x%lx, dst: 0x%lx, size: 0x%lx)\n",
        transfer, burst, burst->src, burst->dst, burst->size);

    if (this->current_size == 0)
    {
        // Last piece, the transfer can now be terminated as soon as all its pieces are done
        transfer->bursts_sent = true;
        this->current_transfer = NULL;
    }

    // Keep last, the next stage may acknowledge synchronously
    this->be->enqueue_transfer(burst);

    if (this->current_transfer != NULL)
    {
        this->fsm_event.enqueue();
    }
    else
    {
        this->fe->update();
    }
}



bool IDmaMeSplit::can_accept_transfer()
{
    return this->current_transfer == NULL;
}



// Called by the next stage to notify the end of a transfer, or of a piece of one
void IDmaMeSplit::ack_transfer(IdmaTransfer *transfer)
{
    if (this->transfer_queue.empty())
    {
        this->trace.fatal("Transfer completion mismatch\n");
    }

    if (transfer == this->transfer_queue.front())
    {
        // Forwarded as is
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Finished transfer (transfer: %p)\n", transfer);
        this->transfer_queue.pop();
        this->fe->ack_transfer(transfer);
    }
    else if (transfer->parent == this->transfer_queue.front())
    {
        // A piece of a cut transfer, which is done once all its pieces are sent and done
        IdmaTransfer *parent = transfer->parent;
        delete transfer;

        parent->nb_bursts--;
        if (parent->bursts_sent && parent->nb_bursts == 0)
        {
            this->trace.msg(vp::Trace::LEVEL_TRACE, "Finished transfer (transfer: %p)\n", parent);
            this->transfer_queue.pop();
            this->fe->ack_transfer(parent);
        }
    }
    else
    {
        this->trace.fatal("Transfer completion mismatch\n");
    }
}



void IDmaMeSplit::reset(bool active)
{
    if (active)
    {
        while (!this->transfer_queue.empty())
        {
            this->transfer_queue.pop();
        }
        this->current_transfer = NULL;
    }
}



void IDmaMeSplit::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaMeSplit *_this = (IDmaMeSplit *)__this;
    _this->send_next(false);
}



void IDmaMeSplit::update()
{
    this->fsm_event.enqueue();
}
