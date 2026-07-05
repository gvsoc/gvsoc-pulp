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
#include "idma_me_dist.hpp"


IDmaMeDist::IDmaMeDist(vp::Component *idma, std::string name, IdmaTransferProducer *fe, std::vector<IdmaTransferConsumer *> be, int be_region_size)
:   Block(idma, name)
{
    // Frontend and backend will be used later for interaction
    this->fe = fe;
    this->nb_be = be.size();
    this->be = be;
    this->transfer_queue.resize(this->nb_be);
    this->be_region_size = be_region_size;
    this->dma_region_start = idma->get_js_config()->get_int("loc_base");
    this->dma_region_end = this->dma_region_start + idma->get_js_config()->get_int("loc_size");

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Get the top parameter giving the maximum number of enqueued transfers
    this->transfer_queue_size = idma->get_js_config()->get_int("transfer_queue_size");
}



// Called by front-end to enqueue transfer
void IDmaMeDist::enqueue_transfer(IdmaTransfer *transfer)
{
    if (!this->can_accept_transfer()) {
        this->trace.fatal("Cannot accept transfer\n");
    }
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Queueing transfer (transfer: %p)\n", transfer);

    int be_region_width = clog2(this->be_region_size);
    int full_region_width = clog2(this->be_region_size * this->nb_be);
    uint64_t be_region_mask = be_region_width >= 64 ? (uint64_t)-1 :
        (((uint64_t)1 << be_region_width) - 1);
    uint64_t full_region_mask = full_region_width >= 64 ? (uint64_t)-1 :
        (((uint64_t)1 << full_region_width) - 1);

    bool src_in_region = transfer->src >= (uint64_t)this->dma_region_start
        && transfer->src < (uint64_t)this->dma_region_end;

    uint64_t src_addr = transfer->src & full_region_mask;
    uint64_t dst_addr = transfer->dst & full_region_mask;
    uint64_t start_addr = src_in_region ? src_addr : dst_addr;
    uint64_t end_addr = start_addr + transfer->size;

    transfer->nb_bursts = 0;

    for (int i = 0; i < this->nb_be; i++) {
        uint64_t region_start = (uint64_t)i * this->be_region_size;
        uint64_t region_end = region_start + this->be_region_size;

        IdmaTransfer *burst = new IdmaTransfer();
        burst->parent = transfer;

        if (start_addr >= region_end || end_addr <= region_start) {
            burst->src = 0;
            burst->dst = 0;
            burst->size = 0;
            burst->reps = 0;
            this->transfer_queue[i].push(burst);
        }
        else if (start_addr >= region_start) {
            // First (and potentially only) slice.
            burst->src = transfer->src;
            burst->dst = transfer->dst;
            if (end_addr <= region_end) {
                burst->size = transfer->size;
            } else {
                burst->size = region_end - start_addr;
            }
            burst->reps = 1;
            this->transfer_queue[i].push(burst);
            transfer->nb_bursts++;
            this->be[i]->enqueue_transfer(burst);
        }
        else {
            // Middle or last slice, align to the region boundary.
            uint64_t offset = region_start - start_addr;
            if (src_in_region) {
                burst->src = (transfer->src & ~full_region_mask) | region_start;
                burst->dst = transfer->dst + offset;
            } else {
                burst->src = transfer->src + offset;
                burst->dst = (transfer->dst & ~full_region_mask) | region_start;
            }
            if (end_addr >= region_end) {
                burst->size = this->be_region_size;
            } else {
                burst->size = end_addr - region_start;
            }
            burst->reps = 1;
            this->transfer_queue[i].push(burst);
            transfer->nb_bursts++;
            this->be[i]->enqueue_transfer(burst);
        }
    }
    transfer->bursts_sent = true;

}



bool IDmaMeDist::can_accept_transfer()
{
    // Accept transfers as soon as there is room in the queue
    for (int i = 0; i < this->nb_be; i++) {
        if (this->transfer_queue[i].size() >= this->transfer_queue_size) {
            return false;
        }
        if (!this->be[i]->can_accept_transfer()) {
            return false;
        }
    }
    return true;
}



// Called by back-end to notify the end of a burst of the transfer
void IDmaMeDist::ack_transfer(IdmaTransfer *transfer)
{
    // Decreased number of pending bursts
    transfer->parent->nb_bursts--;
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Remaining bursts for transfer (transfer: %p, nb_bursts: %d)\n",
        transfer->parent, transfer->parent->nb_bursts);

    // And terminate the transfer if all bursts have been sent and no more burst is pending
    if (transfer->parent->bursts_sent && transfer->parent->nb_bursts == 0)
    {
        // Keep the parent in a local since the loop below frees all the bursts of this
        // transfer, including the one we received, making transfer->parent a dangling read.
        IdmaTransfer *parent = transfer->parent;
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Finished transfer (transfer: %p)\n", parent);
        for (auto &queue: this->transfer_queue) {
            if (queue.empty() || queue.front()->parent != parent) {
                this->trace.fatal("Transfer completion mismatch\n");
            }
            IdmaTransfer *burst = queue.front();
            queue.pop();
            delete burst;
        }
        this->fe->ack_transfer(parent);
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
        // Empty the fifo
        for (auto &queue: this->transfer_queue) {
            while (!queue.empty())
            {
                IdmaTransfer *transfer = queue.front();
                queue.pop();
                // Each transfer needs to be freed since we are owning them
                delete transfer;
            }
        }
    }
}

void IDmaMeDist::update()
{
    this->fe->update();
}

unsigned int IDmaMeDist::clog2(int value)
{
    unsigned int result = 0;
    value--;
    while (value > 0) {
        value >>= 1;
        result++;
    }
    return result;
}