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
#include "idma_me_split.hpp"


IDmaMeSplit::IDmaMeSplit(vp::Component *idma, IdmaTransferProducer *fe, IdmaTransferConsumer *be, int dma_split_size)
:   Block(idma, "me"),
    fsm_event(this, &IDmaMeSplit::fsm_handler)
{
    // Frontend and backend will be used later for interaction
    this->fe = fe;
    this->be = be;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->dma_split_size = dma_split_size;
    this->dma_region_start = idma->get_js_config()->get_int("loc_base");
    this->dma_region_end = this->dma_region_start + idma->get_js_config()->get_int("loc_size");
}



// Called by front-end to enqueue transfer
void IDmaMeSplit::enqueue_transfer(IdmaTransfer *transfer)
{
    if (!this->can_accept_transfer()) {
        this->trace.fatal("Cannot accept transfer\n");
    }
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Queueing transfer (transfer: %p)\n", transfer);
    this->transfer_queue.push(transfer);

    int dma_split_width = clog2(this->dma_split_size);
    uint64_t dma_split_mask = dma_split_width >= 64 ? (uint64_t)-1 :
        (((uint64_t)1 << dma_split_width) - 1);

    bool src_in_region = transfer->src >= (uint64_t)this->dma_region_start
        && transfer->src < (uint64_t)this->dma_region_end;

    uint64_t start_addr = src_in_region ? transfer->src : transfer->dst;
    uint64_t end_addr = start_addr + transfer->size;

    if (dma_split_size - (start_addr & dma_split_mask) >= transfer->size) {
        if (this->be->can_accept_transfer()) {
            this->be->enqueue_transfer(transfer);
        }
        else {
            this->current_transfer = transfer;
            this->current_size = 0;
        }
    }
    else {
        this->current_transfer = transfer;
        this->current_src = transfer->src;
        this->current_dst = transfer->dst;
        this->current_size = transfer->size;

        if (this->be->can_accept_transfer()) {
            IdmaTransfer *burst = new IdmaTransfer();

            // Extract one line from current transfer info
            burst->parent = transfer;
            transfer->nb_bursts++;
            burst->src = transfer->src;
            burst->dst = transfer->dst;
            burst->size = this->dma_split_size - (start_addr & dma_split_mask);
            this->current_src += burst->size;
            this->current_dst += burst->size;
            this->current_size -= burst->size;
            this->be->enqueue_transfer(burst);
            // Re-enqueue FSM to check for next split
            this->fsm_event.enqueue();
        }
    }
}

bool IDmaMeSplit::can_accept_transfer()
{
    return this->current_transfer == NULL;
}



// Called by back-end to notify the end of a burst of the transfer
void IDmaMeSplit::ack_transfer(IdmaTransfer *transfer)
{
    if (this->transfer_queue.empty()) {
        this->trace.fatal("Transfer completion mismatch\n");
    }

    if (transfer == this->transfer_queue.front()) {
        this->transfer_queue.pop();
        this->fe->ack_transfer(transfer);
    }
    else if (transfer->parent == this->transfer_queue.front()) {
        // This was a split transfer, just decrease the number of pending bursts
        transfer->parent->nb_bursts--;

        // And terminate the transfer if all bursts have been sent and no more burst is pending
        if (transfer->parent->bursts_sent && transfer->parent->nb_bursts == 0)
        {
            this->transfer_queue.pop();
            this->fe->ack_transfer(transfer->parent);
        }

        delete transfer;
    }
    else {
        this->trace.fatal("Transfer completion mismatch\n");
    }
}



void IDmaMeSplit::reset(bool active)
{
    if (active)
    {
        // Clear current transfer
        this->current_transfer = NULL;
    }
}



void IDmaMeSplit::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaMeSplit *_this = (IDmaMeSplit *)__this;

    if (_this->current_transfer != NULL && _this->be->can_accept_transfer()) {
        if (_this->current_size == 0) {
            // No split
            _this->be->enqueue_transfer(_this->current_transfer);
            _this->current_transfer = NULL;
            _this->fe->update();
        }
        else {
            IdmaTransfer *burst = new IdmaTransfer();

            // Extract one line from current transfer info
            burst->parent = _this->current_transfer;
            _this->current_transfer->nb_bursts++;
            burst->src = _this->current_src;
            burst->dst = _this->current_dst;
            burst->size = _this->current_size < (uint64_t)_this->dma_split_size ?
                _this->current_size : (uint64_t)_this->dma_split_size;
            _this->current_src += burst->size;
            _this->current_dst += burst->size;
            _this->current_size -= burst->size;
            _this->be->enqueue_transfer(burst);

            // Re-enqueue FSM to check for next split
            if (_this->current_size > 0) {
                _this->fsm_event.enqueue();
            }
            else {
                _this->current_transfer->bursts_sent = true;
                _this->current_transfer = NULL;
                _this->fe->update();
            }
        }
    }
}



void IDmaMeSplit::update()
{
    this->fsm_event.enqueue();
}

unsigned int IDmaMeSplit::clog2(int value)
{
    unsigned int result = 0;
    value--;
    while (value > 0) {
        value >>= 1;
        result++;
    }
    return result;
}