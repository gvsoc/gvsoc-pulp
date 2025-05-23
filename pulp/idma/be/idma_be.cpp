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
    IdmaBeConsumer *loc_be_read, IdmaBeConsumer *loc_be_write,
    IdmaBeConsumer *ext_be_read, IdmaBeConsumer *ext_be_write)
:   Block(idma, "be"),
    fsm_event(this, &IDmaBe::fsm_handler)
{
    // Middle-end and backend protocols will be used later for interaction
    this->me = me;
    this->loc_be_read = loc_be_read;
    this->loc_be_write = loc_be_write;
    this->ext_be_read = ext_be_read;
    this->ext_be_write = ext_be_write;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Get the local area description to differentiate local and remote backend protocols
    this->loc_base = idma->get_js_config()->get_int("loc_base");
    this->loc_size = idma->get_js_config()->get_int("loc_size");
}



IdmaBeConsumer *IDmaBe::get_be_consumer(uint64_t base, uint64_t size, bool is_read)
{
    // Returns local backend if it falls within local area, or external backend otherwise
    bool is_loc = base >= this->loc_base && base + size <= this->loc_base + this->loc_size;
    return is_loc ? (is_read ? this->loc_be_read :  this->loc_be_write) :
        (is_read ? this->ext_be_read : this->ext_be_write);
}


// This is called by middle to push a new transfer. This can called only once no transfer is active
// but before they are fully handled by backend protocols so that transfer can be fully pipelined.
void IDmaBe::enqueue_transfer(IdmaTransfer *transfer)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Queueing burst (burst: %p, src: 0x%x, dst: 0x%x, size: 0x%x)\n",
        transfer, transfer->src, transfer->dst, transfer->size);

    // Remember the transfer as it has to be provided when burst are sent to backend protocols
    this->current_transfer = transfer;

    // Extract information abouth the transfer. This will be used to split it into smaller bursts
    this->current_transfer_size = transfer->size;
    transfer->ack_size = transfer->size;
    this->current_transfer_src = transfer->src;
    this->current_transfer_dst = transfer->dst;
    this->current_transfer_src_be = this->get_be_consumer(transfer->src, transfer->size, true);
    this->current_transfer_dst_be = this->get_be_consumer(transfer->dst, transfer->size, false);

    // Trigger FSM
    this->fsm_event.enqueue();
}


bool IDmaBe::can_accept_transfer()
{
    // Only accept a new transfer if no transfer is being processed. Note that the back-end can
    // start a new transfer as soon as it has been fully forwarded to source and destination
    // back-ends.
    return this->current_transfer_size == 0;
}



void IDmaBe::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaBe *_this = (IDmaBe *)__this;

    // We can send a new transfer if:
    // - a transfer is active
    // - the source backend can accept read burst
    // - the destination backend can accept write burst
    // - if a transfer was previously sent, the source backend is the same of the previous
    // trnasfer is done. This condition is to prevent several source backends to fetch
    // bursts at the same time. Only the same source backend can do it.
    if ((_this->prev_transfer_src_be == NULL
        || _this->prev_transfer_src_be == _this->current_transfer_src_be
        || _this->prev_transfer_src_be->is_empty()) && _this->current_transfer_size > 0
        && _this->current_transfer_src_be->can_accept_burst()
        && _this->current_transfer_dst_be->can_accept_burst())
    {
        uint64_t src = _this->current_transfer_src;
        uint64_t dst = _this->current_transfer_dst;
        uint64_t size = _this->current_transfer_size;

        // Legalize the burst. We choose a burst that fits both backend protocols
        uint64_t burst_size = _this->current_transfer_src_be->get_burst_size(src, size);
        burst_size = _this->current_transfer_dst_be->get_burst_size(dst, burst_size);

        _this->prev_transfer_src_be = _this->current_transfer_src_be;

        // Enqueue the read burst. This will make the source backend start sending read requests
        // to the memory
        _this->current_transfer_src_be->read_burst(_this->current_transfer, src, burst_size);
        // Also enqueue the write burst which will be used only when the source is pushing data,
        // to know where to write it
        _this->current_transfer_dst_be->write_burst(_this->current_transfer, dst, burst_size);

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
bool IDmaBe::is_ready_to_accept_data(IdmaTransfer *transfer)
{
    // Check if the destination backend of the first transfer is ready to accept them
    IdmaBeConsumer *dst_be = this->get_be_consumer(transfer->dst, transfer->size, false);
    return dst_be->can_accept_data();
}


// This is called by the source backend protocol to push a data chunk to the destination
void IDmaBe::write_data(IdmaTransfer *transfer, uint8_t *data, uint64_t size)
{
    // Get destination backend
    IdmaBeConsumer *dst_be = this->get_be_consumer(transfer->dst, transfer->size, false);

    // Update current transfer
    transfer->dst += size;
    transfer->size -= size;

    // And forward data.
    // Note that the source backend already checked that the destination was ready by calling
    // our is_ready_to_accept_data method
    dst_be->write_data(transfer, data, size);
}



// This is called by the destination backend protocol to acknowledged written data
void IDmaBe::ack_data(IdmaTransfer *transfer, uint8_t *data, int size)
{
    // Get the source backend protocol for the first transfer
    IdmaBeConsumer *src_be = this->get_be_consumer(transfer->src, transfer->size, true);

    // And acknowledge the data to it so that the data can be freed
    src_be->write_data_ack(data);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Acknowledging written (size: 0x%x, remaining_size: 0x%x)\n",
        size, transfer->ack_size);

    // Account the acknowledged data
    transfer->ack_size -= size;

    // And in case the whole transfer has been acknowledged, terminate it
    if (transfer->ack_size == 0)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Finished burst (transfer: %p)\n", transfer);

        // And if so, notify the middle end
        this->me->ack_transfer(transfer);
    }
}



void IDmaBe::reset(bool active)
{
    if (active)
    {
        this->current_transfer_size = 0;
        this->prev_transfer_src_be = NULL;
    }
}
