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
#include "idma_me_dist.hpp"



IDmaMeDist::IDmaMeDist(vp::Component *idma, const IdmaConfig &cfg, std::string name,
    IdmaTransferProducer *fe, std::vector<IdmaTransferConsumer *> be, uint64_t region_size)
:   Block(idma, name)
{
    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Previous and next stages will be used later for interaction
    this->fe = fe;
    this->set_be(be);

    this->region_size = region_size;
    this->loc_base = cfg.loc_base;
    this->loc_size = cfg.loc_size;

    // Get the top parameter giving the maximum number of enqueued transfers
    this->transfer_queue_size = cfg.transfer_queue_size;

    if (region_size == 0 || (region_size & (region_size - 1)) != 0)
    {
        this->trace.fatal("Region size must be a power of 2 (region_size: 0x%lx)\n", region_size);
    }
}



// Called by the previous stage to enqueue a transfer
void IDmaMeDist::enqueue_transfer(IdmaTransfer *transfer)
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

    // The regions repeat every full_size bytes, only the offset in this window matters
    uint64_t full_size = this->region_size * this->nb_be;
    uint64_t full_mask = full_size - 1;

    bool src_is_loc = transfer->src >= this->loc_base
        && transfer->src < this->loc_base + this->loc_size;
    uint64_t start_addr = (src_is_loc ? transfer->src : transfer->dst) & full_mask;
    uint64_t end_addr = start_addr + transfer->size;

    if (end_addr > full_size)
    {
        this->trace.fatal("Transfer crosses the window of the regions, it must be split first "
            "(transfer: %p, src: 0x%lx, dst: 0x%lx, size: 0x%lx, window: 0x%lx)\n",
            transfer, transfer->src, transfer->dst, transfer->size, full_size);
    }

    transfer->nb_bursts = 0;
    transfer->bursts_sent = false;

    // Create all the pieces first, so that a synchronous acknowledge cannot see the transfer as
    // done before all of them are sent
    std::vector<IdmaTransfer *> pieces(this->nb_be, NULL);

    for (int i = 0; i < this->nb_be; i++)
    {
        uint64_t region_start = (uint64_t)i * this->region_size;
        uint64_t region_end = region_start + this->region_size;

        IdmaTransfer *burst = new IdmaTransfer();
        burst->parent = transfer;
        burst->reps = 1;
        burst->config = 0;

        if (start_addr >= region_end || end_addr <= region_start)
        {
            // Region not touched, this next stage gets nothing
            burst->src = 0;
            burst->dst = 0;
            burst->size = 0;
        }
        else if (start_addr >= region_start)
        {
            // First, and maybe only, piece
            burst->src = transfer->src;
            burst->dst = transfer->dst;
            burst->size = (end_addr <= region_end ? end_addr : region_end) - start_addr;
            pieces[i] = burst;
        }
        else
        {
            // Middle or last piece, starting at the beginning of the region on the local side
            uint64_t offset = region_start - start_addr;
            if (src_is_loc)
            {
                burst->src = (transfer->src & ~full_mask) | region_start;
                burst->dst = transfer->dst + offset;
            }
            else
            {
                burst->src = transfer->src + offset;
                burst->dst = (transfer->dst & ~full_mask) | region_start;
            }
            burst->size = (end_addr >= region_end ? region_end : end_addr) - region_start;
            pieces[i] = burst;
        }

        this->transfer_queue[i].push(burst);
        if (pieces[i] != NULL)
        {
            transfer->nb_bursts++;
        }
    }

    transfer->bursts_sent = true;
    this->pending_transfers.push(transfer);

    if (transfer->nb_bursts == 0)
    {
        // Empty transfer, it is done already
        this->done_transfers.insert(transfer);
        this->retire_transfers();
        return;
    }

    for (int i = 0; i < this->nb_be; i++)
    {
        if (pieces[i] != NULL)
        {
            this->trace.msg(vp::Trace::LEVEL_TRACE, "Sending piece (transfer: %p, port: %d, piece: %p, src: 0x%lx, dst: 0x%lx, size: 0x%lx)\n",
                transfer, i, pieces[i], pieces[i]->src, pieces[i]->dst, pieces[i]->size);
            this->be[i]->enqueue_transfer(pieces[i]);
        }
    }
}



bool IDmaMeDist::can_accept_transfer()
{
    // A transfer may give a piece to every next stage, so all of them must have room
    for (int i = 0; i < this->nb_be; i++)
    {
        if (this->transfer_queue[i].size() >= this->transfer_queue_size)
        {
            return false;
        }
        if (!this->be[i]->can_accept_transfer())
        {
            return false;
        }
    }
    return true;
}



// Called by a next stage to notify the end of a piece
void IDmaMeDist::ack_transfer(IdmaTransfer *transfer)
{
    IdmaTransfer *parent = transfer->parent;

    parent->nb_bursts--;
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Remaining pieces for transfer (transfer: %p, nb_bursts: %d)\n",
        parent, parent->nb_bursts);

    // The transfer is done once all its pieces are, and terminated once all the older ones are
    if (parent->bursts_sent && parent->nb_bursts == 0)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Finished transfer (transfer: %p)\n", parent);
        this->done_transfers.insert(parent);
        this->retire_transfers();
    }
}



void IDmaMeDist::retire_transfers()
{
    bool retired = false;
    while (!this->pending_transfers.empty() &&
        this->done_transfers.count(this->pending_transfers.front()))
    {
        IdmaTransfer *transfer = this->pending_transfers.front();
        this->pending_transfers.pop();
        this->done_transfers.erase(transfer);
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Terminating transfer (transfer: %p)\n", transfer);

        // Free its pieces, which are at the head of the queues since the older transfers are gone
        for (auto &queue: this->transfer_queue)
        {
            if (queue.empty() || queue.front()->parent != transfer)
            {
                this->trace.fatal("Transfer completion mismatch\n");
            }
            IdmaTransfer *burst = queue.front();
            queue.pop();
            delete burst;
        }
        this->fe->ack_transfer(transfer);
        retired = true;
    }
    if (retired)
    {
        this->fe->update();
    }
}



void IDmaMeDist::set_be(std::vector<IdmaTransferConsumer *> be)
{
    this->be = be;
    this->nb_be = be.size();
    this->transfer_queue.resize(this->nb_be);
}



void IDmaMeDist::reset(bool active)
{
    if (active)
    {
        while (!this->pending_transfers.empty())
        {
            this->pending_transfers.pop();
        }
        this->done_transfers.clear();

        for (auto &queue: this->transfer_queue)
        {
            while (!queue.empty())
            {
                // We own the pieces, free them
                IdmaTransfer *transfer = queue.front();
                queue.pop();
                delete transfer;
            }
        }
    }
}



void IDmaMeDist::update()
{
    this->fe->update();
}
