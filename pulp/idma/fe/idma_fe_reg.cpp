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
#include "idma_fe_reg.hpp"
#include "vp/itf/io.hpp"



IDmaFeReg::IDmaFeReg(vp::Component *idma, IdmaTransferConsumer *me)
    : Block(idma, "fe"),
    src(*this, "src", 32),
    dst(*this, "dst", 32),
    length(*this, "length", 32),
    src_stride(*this, "src_stride", 32),
    dst_stride(*this, "dst_stride", 32),
    reps(*this, "reps", 32),
    next_transfer_id(*this, "next_transfer_id", 32, true, 1),
    completed_id(*this, "completed_id", 32, true, 0),
    do_transfer_grant(*this, "do_transfer_grant", 1),
    trace_busy(*this, "busy", 1, vp::SignalCommon::ResetKind::HighZ),
    trace_src(*this, "src", 32, vp::SignalCommon::ResetKind::HighZ),
    trace_dst(*this, "dst", 32, vp::SignalCommon::ResetKind::HighZ),
    trace_length(*this, "length", 32, vp::SignalCommon::ResetKind::HighZ),
    trace_src_stride(*this, "src_stride", 32, vp::SignalCommon::ResetKind::HighZ),
    trace_dst_stride(*this, "dst_stride", 32, vp::SignalCommon::ResetKind::HighZ),
    trace_reps(*this, "reps", 32, vp::SignalCommon::ResetKind::HighZ),
    trace_id(*this, "id", 1, vp::SignalCommon::ResetKind::HighZ)
{
    // Middle-end will be used later for interaction
    this->me = me;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Declare offload slave interface where instructions will be offloaded
    this->input_itf.set_req_meth(&IDmaFeReg::req);
    idma->new_slave_port("input", &this->input_itf, this);

    idma->new_master_port("irq", &this->irq_itf, this);
}



vp::IoReqStatus IDmaFeReg::req(vp::Block *__this, vp::IoReq *req)
{
    IDmaFeReg *_this = (IDmaFeReg *)__this;

    if (req->get_size() != 4) return vp::IO_REQ_INVALID;

    uint32_t value = *(uint32_t *)req->get_data();

    switch (req->get_addr())
    {
        case 0x08:
            break;

        case 0x10:
        {
            bool granted;
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Trigger transfer\n");
            *(uint32_t *)req->get_data() = _this->enqueue_copy(granted);
            // In case the transfer is not granted, it is just ignored, it is up to the SW to make
            // sure we never overflow the queue
            if (!granted)
            {
                _this->trace.force_warning("Pushing transfer while the queue is already full");
            }
            break;
        }

        case 0x18:
            *(uint32_t *)req->get_data() = _this->completed_id.get();
            break;

        case 0xd0:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received dst address (addr: 0x%llx)\n",
                value);
            _this->dst.set(value);
            break;
        case 0xd8:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received src address (addr: 0x%llx)\n",
                value);
            _this->src.set(value);
            break;
        case 0xe0:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received length (length: 0x%lx)\n", value);
            _this->length.set(value);
            break;
        case 0xe8:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received destination stride (stride: 0x%lx)\n",
                value);
            _this->dst_stride.set(value);
            break;
        case 0xf0:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received source stride (stride: 0x%lx)\n",
                value);
            _this->src_stride.set(value);
            break;
        case 0xf8:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received number of repetition (reps: 0x%lx)\n",
                value);
            _this->reps.set(value);
            break;
    }

    return vp::IO_REQ_OK;
}



uint32_t IDmaFeReg::enqueue_copy(bool &granted)
{
    // Allocate transfer ID
    uint32_t transfer_id = this->next_transfer_id.get();
    this->next_transfer_id.set(transfer_id + 1);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Allocated transfer ID (id: %d)\n", transfer_id);

    // Allocate a new transfer and fill it from registers
    IdmaTransfer *transfer = new IdmaTransfer();
    transfer->src = this->src.get();
    transfer->dst = this->dst.get();
    transfer->size = this->length.get();
    transfer->src_stride = this->src_stride.get();
    transfer->dst_stride = this->dst_stride.get();
    transfer->reps = this->reps.get();
    transfer->config = 1 << 1;

    this->trace_src.set_and_release(this->src.get());
    this->trace_dst.set_and_release(this->dst.get());
    this->trace_length.set_and_release(this->length.get());
    this->trace_src_stride.set_and_release(this->src_stride.get());
    this->trace_dst_stride.set_and_release(this->dst_stride.get());
    this->trace_reps.set_and_release(this->reps.get());
    this->trace_id.set_and_release(transfer_id);
    this->trace_busy = true;

    this->trace.msg(vp::Trace::LEVEL_INFO, "Enqueuing transfer (id: %d, src: %llx, dst: %llx, "
        "size: %llx, src_stride: %llx, dst_stride: %llx, reps: %llx, config: %llx)\n",
        transfer_id, transfer->src, transfer->dst, transfer->size, transfer->src_stride,
        transfer->dst_stride, transfer->reps, transfer->config);

    // In case size is 0 or reps is 0 with 2d transfer, directly terminate the transfer.
    // This could be done few cycles after to better match HW.
    if (this->length == 0 || ((transfer->config >> 1) & 1) && transfer->reps == 0)
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
void IDmaFeReg::ack_transfer(IdmaTransfer *transfer)
{
    this->completed_id.inc(1);
    delete transfer;

    if (this->completed_id == this->next_transfer_id - 1)
    {
        this->trace_busy = false;
    }

    if (this->irq_itf.is_bound())
    {
        this->irq_itf.sync(true);
    }
}



// Called by middle-end when something has been updated to check if we must take any action
void IDmaFeReg::update()
{
    // In case a transfer is blocked and the middle-end is now ready, unblock it and grant it
    // to unstall the core
    if (this->do_transfer_grant.get() && this->me->can_accept_transfer())
    {
        this->do_transfer_grant.set(false);

        this->trace.msg(vp::Trace::LEVEL_TRACE, "Middle-end got ready, unblocking transfer\n");

        IdmaTransfer *transfer = this->stalled_transfer;

        this->me->enqueue_transfer(transfer);
    }
}



void IDmaFeReg::reset(bool active)
{
}
