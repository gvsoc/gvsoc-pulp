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
 *          Lorenzo Ruotolo, Politecnico di Torino (lorenzo.ruotolo@polito.it)
 */

#include <vp/vp.hpp>
#include "idma_fe_cheshire.hpp"



IDmaFeCheshire::IDmaFeCheshire(vp::Component *idma, IdmaTransferConsumer *me)
    : Block(idma, "fe"),
    src(*this, "src", 64),
    dst(*this, "dst", 64),
    num_bytes(*this, "num_bytes", 64),
    config(*this, "config", 64),
    src_stride(*this, "src_stride", 64),
    dst_stride(*this, "dst_stride", 64),
    reps(*this, "reps", 64),
    next_transfer_id(*this, "next_transfer_id", 64, true, 2),
    completed_id(*this, "completed_id", 64, true, 1),
    do_transfer_grant(*this, "do_transfer_grant", 1)
{
    transfer_granted = false;

    // Middle-end will be used later for interaction
    this->me = me;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    
    this->input_itf.set_req_meth(&IDmaFeCheshire::req);

    idma->new_slave_port("input", &this->input_itf, this);
    
    // Declare offload master interface for granting blocked transfers
    idma->new_master_port("offload_grant", &this->offload_grant_itf, this);
}



// Request handler for iDMA access
vp::IoReqStatus IDmaFeCheshire::req(vp::Block *__this, vp::IoReq *req)
{
    IDmaFeCheshire *_this   = (IDmaFeCheshire *)__this;
    uint64_t       offset   = req->get_addr();
    bool           is_write = req->get_is_write();
    uint8_t        *data    = req->get_data();
    uint64_t       size     = req->get_size();

    _this->trace.msg("iDMA access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, req->get_is_write());

    switch (offset)
    {

    /* WRITES */

    case IDMA_REG64_2D_FRONTEND_SRC_ADDR_REG_OFFSET:
        // Set source address for 2D transfer
        if (is_write && size == 8) {
            _this->trace.msg("Received src address write (addr: 0x%lx)\n", *(uint64_t *)data);
            _this->src.set(*(uint64_t *)data);
        }
        break;

    case IDMA_REG64_2D_FRONTEND_DST_ADDR_REG_OFFSET:
        // Set destination address for 2D transfer
        if (is_write && size == 8) {
            _this->trace.msg("Received dst address write (addr: 0x%lx)\n", *(uint64_t *)data);
            _this->dst.set(*(uint64_t *)data);
        }
        break;

    case IDMA_REG64_2D_FRONTEND_NUM_BYTES_REG_OFFSET:
        // Set number of bytes to transfer for 2D transfer
        if (is_write && size == 8) {
            _this->trace.msg("Received number of bytes write (bytes: 0x%lx)\n", *(uint64_t *)data);
            _this->num_bytes.set(*(uint64_t *)data);
        }
        break;

    case IDMA_REG64_2D_FRONTEND_STRIDE_SRC_REG_OFFSET:
        // Set source stride for 2D transfer
        if (is_write && size == 8) {
            _this->trace.msg("Received source stride write (stride: 0x%lx)\n", *(uint64_t *)data);
            _this->src_stride.set(*(uint64_t *)data);
        }
        break;

    case IDMA_REG64_2D_FRONTEND_STRIDE_DST_REG_OFFSET:
        // Set destination stride for 2D transfer
        if (is_write && size == 8) {
            _this->trace.msg("Received destination stride write (stride: 0x%lx)\n", *(uint64_t *)data);
            _this->dst_stride.set(*(uint64_t *)data);
        }
        break;

    case IDMA_REG64_2D_FRONTEND_NUM_REPETITIONS_REG_OFFSET:
        // Set number of repetitions for 2D transfer
        if (is_write && size == 8) {
            _this->trace.msg("Received number of repetitions write (reps: 0x%lx)\n", *(uint64_t *)data);
            _this->reps.set(*(uint64_t *)data);
        }
        break;

    case IDMA_REG64_2D_FRONTEND_CONF_REG_OFFSET:
        // Set configuration for 2D transfer
        if (is_write && size == 8) {
            _this->trace.msg("Received configuration write (config: 0x%lx)\n", *(uint64_t *)data);
            _this->config.set(*(uint64_t *)data);
        }
        break;


    /* READS */

    case IDMA_REG64_2D_FRONTEND_NEXT_ID_REG_OFFSET:
        // Read next transfer ID, this will also trigger the transfer
        if (!is_write && size == 8) {
            _this->trace.msg("Received next transfer ID read (id: %d)\n", _this->next_transfer_id.get());
            *(uint64_t *)data = _this->enqueue_copy();
        }
        break;
            
    case IDMA_REG64_2D_FRONTEND_DONE_REG_OFFSET:
        // Read completed transfer ID
        if (!is_write && size == 8) {
            _this->trace.msg("Received completed transfer ID read (id: %d)\n", _this->completed_id.get());
            *(uint64_t *)data = _this->completed_id.get();
        }
        break;

    default:
        _this->trace.force_warning("Unhandled iDMA register access\n");
        return vp::IO_REQ_INVALID;
    }

    return vp::IO_REQ_OK;
}

uint64_t IDmaFeCheshire::enqueue_copy(void)
{
    // Allocate transfer ID
    uint64_t transfer_id = this->next_transfer_id.get();
    this->next_transfer_id.set(transfer_id + 1);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Allocated transfer ID (id: %d)\n", transfer_id);

    // Allocate a new transfer and fill it from registers
    IdmaTransfer *transfer = new IdmaTransfer();
    transfer->src        = this->src.get();
    transfer->dst        = this->dst.get();
    transfer->size       = this->num_bytes.get();
    transfer->src_stride = this->src_stride.get();
    transfer->dst_stride = this->dst_stride.get();
    transfer->reps       = this->reps.get();
    transfer->config     = this->config.get();

    this->trace.msg(vp::Trace::LEVEL_INFO, "Enqueuing transfer (id: %d, src: %llx, dst: %llx, "
        "size: %llx, src_stride: %llx, dst_stride: %llx, reps: %llx, config: %llx)\n",
        transfer_id, transfer->src, transfer->dst, transfer->size, transfer->src_stride,
        transfer->dst_stride, transfer->reps, transfer->config);

    // In case size is 0 or reps is 0 with 2d transfer, directly terminate the transfer.
    //  This could be done few cycles after to better match HW.
    if (transfer->size == 0 || ((transfer->config >> 1) & 1) && transfer->reps == 0)
    {
        this->ack_transfer(transfer);
        return transfer_id;
    }

    // Check if middle end can accept a new transfer
    if (this->me->can_accept_transfer())
    {
        // If no enqueue the burst
        this->transfer_granted = true;
        this->me->enqueue_transfer(transfer);
    }
    else
    {
        // Otherwise, stall the core and keep the transfer until we can grant it
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Middle-end not ready, blocking transfer\n");
        this->stalled_transfer = transfer;
        this->transfer_granted = false;

        this->do_transfer_grant.set(true);
    }

    return transfer_id;
}



// Called by middle-end when a transfer is done
void IDmaFeCheshire::ack_transfer(IdmaTransfer *transfer)
{
    this->completed_id.inc(1);
    delete transfer;
}



// Called by middle-end when something has been updated to check if we must take any action
void IDmaFeCheshire::update()
{
    // In case a transfer is blocked and the middle-end is now ready, unblock it and grant it
    // to unstall the core
    if (this->do_transfer_grant.get() && this->me->can_accept_transfer())
    {
        this->do_transfer_grant.set(false);

        this->trace.msg(vp::Trace::LEVEL_TRACE, "Middle-end got ready, unblocking transfer\n");

        IdmaTransfer *transfer = this->stalled_transfer;

        IssOffloadInsnGrant<uint64_t> offload_grant = {
            .result=this->next_transfer_id.get() - 1
        };
        this->offload_grant_itf.sync(&offload_grant);
        this->me->enqueue_transfer(transfer);
    }
}



void IDmaFeCheshire::reset(bool active)
{
}