/*
 * Copyright (C) 2026 Fondazione Chips-IT
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
 * Authors: Lorenzo Zuolo, Fondazione Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <vp/vp.hpp>
#include "idma_me_3d.hpp"


IDmaMe3d::IDmaMe3d(vp::Component *idma, IdmaTransferProducer *fe, IdmaTransferConsumer *be)
    : Block(idma, "me"),
    fsm_event(this, &IDmaMe3d::fsm_handler),
    me_state(*this, "me_state", 32, true, ME3D_IDLE)
{
    // Frontend and backend will be used later for interaction
    this->fe = fe;
    this->be = be;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Get the top parameter giving the maximum number of enqueued transfers
    this->transfer_queue_size = idma->get_js_config()->get_int("transfer_queue_size");

    this->current_transfer = NULL;
}



// Called by front-end to enqueue transfer
void IDmaMe3d::enqueue_transfer(IdmaTransfer *transfer)
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



bool IDmaMe3d::can_accept_transfer()
{
    // Accept transfers as soon as there is room in the queue
    return this->transfer_queue.size() < (size_t)this->transfer_queue_size;
}



// Called by back-end to notify the end of a burst of the transfer
void IDmaMe3d::ack_transfer(IdmaTransfer *transfer)
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



void IDmaMe3d::reset(bool active)
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



void IDmaMe3d::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaMe3d *_this = (IDmaMe3d *)__this;

    // Check if one of the queued transfer can become the current one
    if (_this->transfer_queue.size() > 0 && _this->current_transfer == NULL)
    {
        IdmaTransfer *transfer = _this->transfer_queue.front();

        // Extract transfer information to keep track of current burst
        _this->current_transfer = transfer;
        _this->current_src = transfer->src;
        _this->current_dst = transfer->dst;
        // A 1D transfer is a 2D transfer with a single line, and a 2D transfer is a 3D transfer
        // with a single page. Normalizing here keeps a single control path below. A transfer
        // produced by another front-end carries no third dimension, so treat it as a single page.
        bool has_nd_data = transfer->data.size() >= IDMA_ND_DATA_SIZE;

        _this->current_reps_init =
            (transfer->config & IDMA_CONFIG_2D) ? transfer->reps : 1;
        _this->current_reps = _this->current_reps_init;

        _this->current_reps_3d =
            ((transfer->config & IDMA_CONFIG_3D) && has_nd_data)
            ? transfer->data[IDMA_ND_REPS_3] : 1;

        _this->me_state.set(ME3D_DECOMPOSING);
    }

    // Check if we can extract a burst from the current transfer
    if (_this->current_transfer != NULL && _this->be->can_accept_transfer())
    {
        IdmaTransfer *transfer = _this->current_transfer;

        // Create a burst
        IdmaTransfer *burst = new IdmaTransfer();

        // Extract one line from current transfer info
        burst->parent = transfer;
        transfer->nb_bursts++;
        burst->src = _this->current_src;
        burst->dst = _this->current_dst;
        burst->size = transfer->size;
        _this->current_reps--;

        if (_this->current_reps == 0)
        {
            // End of the current page, move to the next one
            _this->current_reps_3d--;

            if (_this->current_reps_3d == 0)
            {
                // End of transfer, mark it as fully sent
                transfer->bursts_sent = true;

                // And remove it
                _this->current_transfer = NULL;
                _this->transfer_queue.pop();
                _this->me_state.set(ME3D_IDLE);

                // Update frontend in case it has a transfer to queue
                _this->fe->update();
            }
            else
            {
                // The stride accumulates on the address of the line just emitted, matching
                // idma_nd_midend, so the next page starts at (reps - 1) * stride_2 + stride_3
                // from the current one.
                _this->current_src += transfer->data[IDMA_ND_SRC_STRIDE_3];
                _this->current_dst += transfer->data[IDMA_ND_DST_STRIDE_3];
                _this->current_reps = _this->current_reps_init;
            }
        }
        else
        {
            // Otherwise, switch to next line
            _this->current_src += transfer->src_stride;
            _this->current_dst += transfer->dst_stride;
        }

        // Enqueue line to backend
        _this->be->enqueue_transfer(burst);

        // And trigger again FSM for next line
        _this->fsm_event.enqueue();
    }
}



void IDmaMe3d::update()
{
    this->fsm_event.enqueue();
}
