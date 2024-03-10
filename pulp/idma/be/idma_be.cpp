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
#include "idma_be.hpp"



IDmaBe::IDmaBe(vp::Component *idma, IdmaTransferProducer *me,
    IdmaBeConsumer *loc_be, IdmaBeConsumer *ext_be)
:   Block(idma, "be"),
    fsm_event(this, &IDmaBe::fsm_handler)
{
    // Middle-end and backend protocols will be used later for interaction
    this->me = me;
    this->loc_be = loc_be;
    this->ext_be = ext_be;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Get the local area description to differentiate local and remote backend protocols
    this->loc_base = idma->get_js_config()->get_int("loc_base");
    this->loc_size = idma->get_js_config()->get_int("loc_size");
}



IdmaBeConsumer *IDmaBe::get_be_consumer(uint64_t base, uint64_t size)
{
    // Returns local backend if it falls within local area, or external backend otherwise
    bool is_loc = base >= this->loc_base && base + size <= this->loc_base + this->loc_size;
    return is_loc ? this->loc_be : this->ext_be;
}


// This is called by middle to push a new transfer. This is called only when the backend has
// no active transfer
void IDmaBe::enqueue_transfer(IdmaTransfer *transfer)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Queueing burst (burst: %p)\n", transfer);

    // Push the transfer into the queue, we will need it later when the bursts are coming back
    // from memory. We will remove it from the queue when the transfer is fully done
    this->transfer_queue.push(transfer);

    // Extract information abouth the transfer
    this->current_transfer = transfer;
    this->current_transfer_size = transfer->size;
    this->current_transfer_src = transfer->src;
    this->current_transfer_dst = transfer->dst;
    this->current_transfer_src_be = this->get_be_consumer(transfer->src, transfer->size);
    this->current_transfer_dst_be = this->get_be_consumer(transfer->dst, transfer->size);

    this->fsm_event.enqueue();
}


bool IDmaBe::can_accept_transfer()
{
    // Only accept a new transfer if no transfer is on-going
    return this->current_transfer_size == 0;
}



void IDmaBe::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaBe *_this = (IDmaBe *)__this;

    // If a transfer is active and the source backend protocol is ready to accept read bursts,
    // we can send a new one.
    if (_this->current_transfer_size > 0 && _this->current_transfer_src_be->can_accept_burst()
         && _this->current_transfer_dst_be->can_accept_burst())
    {
        uint64_t src = _this->current_transfer_src;
        uint64_t dst = _this->current_transfer_dst;
        uint64_t size = _this->current_transfer_size;

        // Legalize the burst. We choose a burst that fits both backend protocols
        uint64_t burst_size = _this->current_transfer_src_be->get_burst_size(src, size);
        burst_size = _this->current_transfer_dst_be->get_burst_size(dst, burst_size);

        // Enqueue the read burst. This will make the source backend start sending read requests
        // to the memory
        _this->current_transfer_src_be->read_burst(src, burst_size);
        // Also enqueue the write burst which will be used only when the source is pushing data,
        // to know where to write it
        _this->current_transfer_dst_be->write_burst(dst, burst_size);

        // Updated current transfer by removing the burst we just processed
        _this->current_transfer_size -= burst_size;
        _this->current_transfer_src += burst_size;
        _this->current_transfer_dst += burst_size;

        // Retry to send a burst in the next cycle
        _this->fsm_event.enqueue();

        if (_this->current_transfer_size == 0)
        {
            // In case the transfer is finished, the middle-end may push a new one
            _this->me->update();
        }
    }
}



void IDmaBe::update()
{
    // Check if any action should be taken in the next cycle from the FSM handler
    this->fsm_event.enqueue();
}



// Called by source backend protocol to know if it can send data to be written
bool IDmaBe::is_ready_to_accept_data()
{
    // Check if the destination backend of the first transfer is ready to accept them
    IdmaTransfer *transfer = this->transfer_queue.front();
    IdmaBeConsumer *dst_be = this->get_be_consumer(transfer->dst, transfer->size);
    return dst_be->can_accept_data();
}


// This is called by the source backend protocol to push a data chunk to the destination
void IDmaBe::write_data(uint8_t *data, uint64_t size)
{
    // Get back the first transfer from the queue to know where to send the data
    IdmaTransfer *transfer = this->transfer_queue.front();
    // Get destination backend
    IdmaBeConsumer *dst_be = this->get_be_consumer(transfer->dst, transfer->size);

    // Update current transfer
    transfer->dst += size;
    transfer->size -= size;

    // And forward data.
    // Note that the source backend already checked that the destination was ready by calling
    // our is_ready_to_accept_data method
    dst_be->write_data(data, size);
}



// This is called by the destination backend protocol to acknowledged written data
void IDmaBe::ack_data(uint8_t *data)
{
    // Get the source backend protocol for the first transfer
    IdmaTransfer *transfer = this->transfer_queue.front();
    IdmaBeConsumer *src_be = this->get_be_consumer(transfer->src, transfer->size);

    // And acknowledge the data to it so that the data can be freed
    src_be->write_data_ack(data);

    // Also check if the transfer is done
    if (transfer->size == 0)
    {
        // And if so, remove it and notify the middle end
        this->transfer_queue.pop();
        this->me->ack_transfer(transfer);
    }
}



void IDmaBe::reset(bool active)
{
    if (active)
    {
        this->current_transfer_size = 0;
    }
}
