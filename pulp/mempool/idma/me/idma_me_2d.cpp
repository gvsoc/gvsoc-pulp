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
#include "idma_me_2d.hpp"


IDmaMe2D::IDmaMe2D(vp::Component *idma, IdmaTransferProducer *fe, IdmaTransferConsumer *be)
:   Block(idma, "me"),
    fsm_event(this, &IDmaMe2D::fsm_handler)
{
    // Frontend and backend will be used later for interaction
    this->fe = fe;
    this->be = be;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Get the top parameter giving the maximum number of enqueued transfers
    this->transfer_queue_size = idma->get_js_config()->get_int("transfer_queue_size");
}



// Called by front-end to enqueue transfer
void IDmaMe2D::enqueue_transfer(IdmaTransfer *transfer)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Queueing transfer (transfer: %p)\n", transfer);

    // Number of bursts will be used when they are acknowledged to know when transfer is done
    transfer->nb_bursts = 0;
    transfer->bursts_sent = false;

    // Enqueue the transfer
    this->transfer_queue.push(transfer);

    // And trigger the FSM to check if the transfer must be handled
    this->fsm_event.enqueue();
}



bool IDmaMe2D::can_accept_transfer()
{
    // Accept transfers as soon as there is room in the queue
    return this->transfer_queue.size() < this->transfer_queue_size;
}



// Called by back-end to notify the end of a burst of the transfer
void IDmaMe2D::ack_transfer(IdmaTransfer *transfer)
{
    // Decreased number of pending bursts
    transfer->parent->nb_bursts--;

    // And terminate the transfer if all bursts have been sent and no more burst is pending
    if (transfer->parent->bursts_sent && transfer->parent->nb_bursts == 0)
    {
        this->fe->ack_transfer(transfer->parent);
    }

    delete transfer;
}



void IDmaMe2D::reset(bool active)
{
    if (active)
    {
        // Empty the fifo
        while (this->transfer_queue.size() > 0)
        {
            IdmaTransfer *transfer = this->transfer_queue.front();
            this->transfer_queue.pop();
            // Each transfer needs to be freed since we are owning them
            delete transfer;
        }

        // Clear current transfer
        this->current_transfer = NULL;
    }
}



void IDmaMe2D::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaMe2D *_this = (IDmaMe2D *)__this;

    // Check if one of the queued transfer can become the current one
    if (_this->transfer_queue.size() > 0 && _this->current_transfer == NULL)
    {
        // Extract transfer information to keep track of current burst
        _this->current_transfer = _this->transfer_queue.front();
        _this->current_src = _this->current_transfer->src;
        _this->current_dst = _this->current_transfer->dst;
        _this->current_reps = _this->current_transfer->reps;

        // In case it is a 1D transfer, turn it into a 2D transfer to simplify control
        if (((_this->current_transfer->config >> 1) & 1) == 0)
        {
            _this->current_reps = 1;
        }
    }

    // Check if we can extract a burst from the current transfer
    if (_this->current_transfer != NULL && _this->be->can_accept_transfer())
    {
        // Create a burst
        IdmaTransfer *burst = new IdmaTransfer();

        // Extract one line from current transfer info
        burst->parent = _this->current_transfer;
        _this->current_transfer->nb_bursts++;
        burst->src = _this->current_src;
        burst->dst = _this->current_dst;
        burst->size = _this->current_transfer->size;
        _this->current_reps--;

        if (_this->current_reps == 0)
        {
            // End of transfer, mark it as fully sent
            _this->current_transfer->bursts_sent = true;

            // And remove it
            _this->current_transfer = NULL;
            _this->transfer_queue.pop();

            // Update frontend in case it has a transfer to queue
            _this->fe->update();
        }
        else
        {
            // Otherwise, switch to next line
            _this->current_src += _this->current_transfer->src_stride;
            _this->current_dst += _this->current_transfer->dst_stride;
        }

        // Enqueue line to backend
        _this->be->enqueue_transfer(burst);

        // And trigger again FSM for next line
        _this->fsm_event.enqueue();
    }
}



void IDmaMe2D::update()
{
    this->fsm_event.enqueue();
}