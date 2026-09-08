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
#include <algorithm>
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
    auto gather = idma->get_js_config()->get("gather_enable");
    this->gather_enable = gather && gather->get_bool();
    if (this->gather_enable)
    {
        this->index_itf.set_resp_meth(&IDmaMe2D::index_response);
        // Retain denied requests until their response, without reissuing them.
        this->index_itf.set_grant_meth(&IDmaMe2D::index_grant);
        idma->new_master_port("index", &this->index_itf, this);
    }
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
        this->index_pending = false;
        this->index_valid = false;
        this->index_ready_cycle = -1;
        this->index_lane = 0;
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

        if (_this->current_transfer->gather)
        {
            uint64_t stride = _this->current_transfer->src_stride;
            if (!_this->gather_enable || !_this->index_itf.is_bound())
                _this->trace.fatal("Gather requires gather_enable and a bound index port\n");
            if (!stride || (stride & (stride - 1)))
                _this->trace.fatal("Gather source stride must be a nonzero power of two\n");
            _this->index_ptr = _this->current_transfer->index_addr;
            if (_this->index_ptr & 7)
                _this->trace.fatal("Gather index stream must be aligned to a 64-bit TCDM word\n");
            _this->index_valid = false;
            _this->index_lane = 0;
            _this->fsm_event.enqueue();
            return; // Match the gather extension's registered idle-to-run transition.
        }

        // In case it is a 1D transfer, turn it into a 2D transfer to simplify control
        if (((_this->current_transfer->config >> 1) & 1) == 0)
        {
            _this->current_reps = 1;
        }
    }

    if (_this->current_transfer && _this->current_transfer->gather)
    {
        _this->gather_step();
        return;
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


void IDmaMe2D::index_response(vp::Block *__this, vp::IoReq *req)
{
    static_cast<IDmaMe2D *>(__this)->index_completed(req);
}

void IDmaMe2D::index_completed(vp::IoReq *req)
{
    // Even an inline memory response becomes visible on a later clock edge.
    this->index_ready_cycle = this->clock.get_cycles()
        + std::max<uint64_t>(1, req->get_latency());
    this->fsm_event.enqueue(std::max<uint64_t>(1, req->get_latency()));
}

void IDmaMe2D::gather_step()
{
    if (this->index_pending && this->index_ready_cycle > this->clock.get_cycles())
        this->fsm_event.enqueue(this->index_ready_cycle - this->clock.get_cycles());
    if (this->index_pending && this->index_ready_cycle >= 0
        && this->index_ready_cycle <= this->clock.get_cycles())
    {
        this->index_word = 0;
        for (unsigned i = 0; i < 8; ++i)
            this->index_word |= uint64_t(this->index_data[i]) << (8 * i);
        this->index_pending = false;
        this->index_valid = true;
        this->index_ready_cycle = -1;
    }

    if (this->index_valid && this->be->can_accept_transfer())
    {
        IdmaTransfer *parent = this->current_transfer;
        unsigned bits = 8U << parent->index_width;
        unsigned lanes = 64 / bits;
        uint64_t mask = bits == 64 ? UINT64_MAX : (uint64_t(1) << bits) - 1;
        uint64_t index = (this->index_word >> (this->index_lane * bits)) & mask;
        IdmaTransfer *row = new IdmaTransfer();
        row->parent = parent;
        row->src = parent->src + index * parent->src_stride;
        row->dst = this->current_dst;
        row->size = parent->size;
        row->transfer_id = parent->transfer_id;
        parent->nb_bursts++;
        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "GATHER_ROW id=%u index=%llu src=0x%llx dst=0x%llx cycle=%lld\n",
            parent->transfer_id, index, row->src, row->dst, this->clock.get_cycles());
        this->current_dst += parent->dst_stride;
        --this->current_reps;
        if (++this->index_lane == lanes || this->current_reps == 0)
        {
            this->index_lane = 0;
            this->index_valid = false;
        }
        if (this->current_reps == 0)
        {
            parent->bursts_sent = true;
            this->current_transfer = nullptr;
            this->transfer_queue.pop();
            this->fe->update();
        }
        this->be->enqueue_transfer(row);
        this->fsm_event.enqueue();
    }

    // spill_ready permits the next read on the cycle the last lane is used.
    if (this->current_transfer && !this->index_valid && !this->index_pending)
    {
        this->index_req.prepare();
        this->index_req.set_addr(this->index_ptr);
        this->index_req.set_size(8);
        this->index_req.set_is_write(false);
        this->index_req.set_data(this->index_data);
        this->index_pending = true;
        this->index_ptr += 8;
        this->trace.msg(vp::Trace::LEVEL_TRACE, "INDEX_READ addr=0x%llx cycle=%lld\n",
            this->index_req.get_addr(), this->clock.get_cycles());
        auto status = this->index_itf.req(&this->index_req);
        if (status == vp::IO_REQ_OK)
            this->index_completed(&this->index_req);
        else if (status == vp::IO_REQ_INVALID)
            this->trace.fatal("Invalid gather index read at 0x%llx\n",
                this->index_req.get_addr());
        // PENDING and DENIED retain the request until the response callback.
    }
}
