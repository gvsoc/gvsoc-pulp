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

#include <algorithm>
#include <vp/vp.hpp>
#include "idma_be_tcdm_banks.hpp"



IDmaBeTcdmBanks::IDmaBeTcdmBanks(vp::Component *idma, std::string itf_name, IdmaBeProducer *be)
:   Block(idma, itf_name),
    fsm_event(this, &IDmaBeTcdmBanks::fsm_handler)
{
    // Backend will be used later for interaction
    this->be = be;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Get the width of one bank port. This is used to constrain the size of the requests which
    // are sent to a single bank
    this->width = idma->get_js_config()->get_int("tcdm_width");

    // Number of bank ports this channel is split over
    this->nb_banks = idma->get_js_config()->get_int("nb_tcdm_banks");
    if (this->nb_banks < 1)
    {
        this->nb_banks = 1;
    }

    // One master port and one request per bank, so that a whole line can be issued in one cycle
    this->reqs.resize(this->nb_banks);
    for (int i = 0; i < this->nb_banks; i++)
    {
        vp::IoMaster *itf = new vp::IoMaster();
        idma->new_master_port(itf_name + "_" + std::to_string(i), itf);
        this->ico_itf.push_back(itf);
    }

    // Max number of pending bursts
    this->burst_queue_maxsize = idma->get_js_config()->get_int("burst_queue_size");

    // Local memory base
    this->loc_base = idma->get_js_config()->get_int("loc_base");
}



void IDmaBeTcdmBanks::activate_burst()
{
    // If queue is not empty and we don't have any active burst, activate it
    if (this->current_burst_size == 0 && this->burst_queue_size.size() > 0)
    {
        this->current_burst_base = this->burst_queue_base.front();
        this->current_burst_size = this->burst_queue_size.front();
    }
}



void IDmaBeTcdmBanks::enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer)
{
    // Just enqueue the burst and trigger the FSM, the FSM will take care of sending the requests
    this->burst_queue_base.push(base);
    this->burst_queue_size.push(size);
    this->burst_queue_is_write.push(is_write);
    this->burst_queue_transfer.push(transfer);

    // We may need to activate the first burst
    this->activate_burst();

    // Trigger the FSM since we may need to start processing a burst
    this->fsm_event.enqueue();
}



// Called by the backend to enqueue a read burst
void IDmaBeTcdmBanks::read_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, false, transfer);
}



// Called by the backend to enqueue a write burst
void IDmaBeTcdmBanks::write_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size)
{
    this->enqueue_burst(base, size, true, transfer);
}



uint64_t IDmaBeTcdmBanks::get_burst_size(uint64_t base, uint64_t size)
{
    // There is no constraint on burst size, this backend will anyway cut the burst into lines
    return size;
}


bool IDmaBeTcdmBanks::can_accept_burst()
{
    // Accept a burst if we have room in the queue of pending bursts
    return this->burst_queue_base.size() < (size_t)this->burst_queue_maxsize;
}

bool IDmaBeTcdmBanks::can_accept_data()
{
    // Accept data if we don't have already a chunk of data being written
    return this->write_current_chunk_size == 0 && this->write_ack_timestamp == -1;
}



// Called by backend to push data for the current burst
void IDmaBeTcdmBanks::write_data(IdmaTransfer *transfer, uint8_t *data, uint64_t size)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Writing data (size: 0x%lx)\n", size);

    // Since the data may be bigger than a line, first enqueue the whole data and process it
    // line by line
    this->write_current_chunk_ack_size = size;
    this->write_current_chunk_size = size;
    this->write_current_chunk_base = this->current_burst_base;
    this->write_current_chunk_data = data;
    this->write_current_chunk_data_start = data;
    this->write_current_transfer = transfer;

    // Send a line now, the rest will be handled by the FSM
    this->write_line();
}



void IDmaBeTcdmBanks::reset(bool active)
{
    if (active)
    {
        this->current_burst_size = 0;
        this->read_pending_line_size = 0;

        this->write_current_chunk_size = 0;
        this->write_ack_timestamp = -1;

        this->last_line_timestamp = -1;
    }
}



// Issue one line as one access per covered bank. mem_to_banks drives the banks in the same cycle
// and joins their handshakes, so the line completes when the slowest bank does.
int64_t IDmaBeTcdmBanks::access_banks(uint64_t base, uint64_t size, uint8_t *data, bool is_write)
{
    int64_t latency = 0;
    uint64_t offset = 0;

    while (offset < size)
    {
        uint64_t addr = base + offset;

        // Words are interleaved over the banks, the way mem_to_banks distributes them
        int bank = (addr / this->width) % this->nb_banks;

        // Never cross a bank boundary within a single access
        uint64_t chunk = std::min(size - offset, this->width - (addr % this->width));

        vp::IoReq *req = &this->reqs[bank];
        req->prepare();
        req->set_is_write(is_write);
        req->set_addr(addr - this->loc_base);
        req->set_size(chunk);
        req->set_data(data + offset);

        vp::IoReqStatus status = this->ico_itf[bank]->req(req);
        if (status == vp::IoReqStatus::IO_REQ_INVALID)
        {
            trace.force_warning("Invalid access during TCDM %s line (base: 0x%lx, size: 0x%lx)\n",
                is_write ? "write" : "read", addr, chunk);
        }
        else if (status != vp::IoReqStatus::IO_REQ_OK)
        {
            // For now aynchronous replies are not supported since dma is always passing.
            // This could be needed if we want to model a more dynamic priority
            trace.fatal("Asynchronous response is not supported on TCDM backend\n");
        }

        latency = std::max(latency, (int64_t)req->get_latency());
        offset += chunk;
    }

    return latency;
}


void IDmaBeTcdmBanks::write_line()
{
    // Only send the line if no line was already sent in this cycle. Since we try to send a line as
    // soon as we receive a request, this may happen
    if (this->last_line_timestamp == -1 || this->last_line_timestamp < this->clock.get_cycles())
    {
        this->last_line_timestamp = this->clock.get_cycles();

        // Extract one line from current data chunk
        uint64_t base = this->write_current_chunk_base;
        uint64_t size = this->get_line_size(this->write_current_chunk_base, this->write_current_chunk_size);

        this->trace.msg(vp::Trace::LEVEL_TRACE, "Writing line to TCDM (base: 0x%lx, size: 0x%lx)\n",
            base, size);

        uint8_t *data = this->write_current_chunk_data;

        // Update chunk info for next line
        this->write_current_chunk_base += size;
        this->write_current_chunk_size -= size;
        this->write_current_chunk_data += size;

        // Send the line to TCDM, one access per bank
        int64_t latency = this->access_banks(base, size, data, true);

        if (latency == 0)
        {
            // If the response has no latency, handle it now so that we can immediately continue
            // with the next line
            this->remove_chunk_from_current_burst(size);
            this->write_handle_req_ack();
        }
        else
        {
            // Otherwise enqueue it with timestamp so that we acknowledge it at correct time
            this->write_ack_timestamp = this->clock.get_cycles() + latency;
            this->write_ack_size = size;
            this->fsm_event.enqueue(latency);
        }
    }
    else
    {
        this->update();
    }
}



void IDmaBeTcdmBanks::write_handle_req_ack()
{
    if (this->write_current_chunk_size == 0)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Finished TCDM line, notifying middle-end\n");

        this->be->update();
        // If the chunk is done, acknowledge it
        this->be->ack_data(this->write_current_transfer, this->write_current_chunk_data_start, this->write_current_chunk_ack_size);
    }
    else
    {
        // Otherwise, the FSM will take care of the next line
        this->fsm_event.enqueue();
    }
}



uint64_t IDmaBeTcdmBanks::get_line_size(uint64_t base, uint64_t size)
{
    // A line spans every bank, so its size is the width of one bank times the number of banks
    uint64_t line_width = (uint64_t)this->width * this->nb_banks;

    // Make sure we don't go over interface width
    size = std::min(size, line_width);

    // And that we don't cross the line
    uint64_t next_page = (base + line_width - 1) & ~(line_width - 1);
    if (next_page > base)
    {
        size = std::min(next_page - base, size);
    }

    return size;
}



void IDmaBeTcdmBanks::remove_chunk_from_current_burst(uint64_t size)
{
    // Update current burst
    this->current_burst_base += size;
    this->current_burst_size -= size;

    if (this->current_burst_size == 0)
    {
        // In case it is the last chunk, remove the current burst
        this->burst_queue_base.pop();
        this->burst_queue_size.pop();
        this->burst_queue_is_write.pop();
        this->burst_queue_transfer.pop();

        // And take the next one
        this->activate_burst();

        this->be->update();
        this->update();
    }
}


void IDmaBeTcdmBanks::read_line()
{
    // Extract line from current read burst
    uint64_t base = this->current_burst_base;
    uint64_t size = this->get_line_size(base, this->current_burst_size);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Reading line from TCDM (base: 0x%lx, size: 0x%lx)\n",
            base, size);

    // Since the destination backend may keep the data until the write is done, we need
    // to dynamically allocate the data since we may read several times before data is acknowledged
    // We will free it when we receive the ack. The banks fill their own slice of this buffer.
    uint8_t *data = new uint8_t[size];

    // Send the line to TCDM, one access per bank
    int64_t latency = this->access_banks(base, size, data, false);

    if (latency == 0 && this->be->is_ready_to_accept_data(this->burst_queue_transfer.front()))
    {
        // If there is no latency and backend is ready, we can immediately push the data
        IdmaTransfer *transfer = this->burst_queue_transfer.front();
        this->remove_chunk_from_current_burst(size);
        this->be->write_data(transfer, data, size);
    }
    else
    {
        // Otherwise we have to put it on hold since we can only have one line pending
        this->read_pending_timestamp = this->clock.get_cycles() + latency;
        this->read_pending_line_data = data;
        this->read_pending_line_size = size;
        this->fsm_event.enqueue(latency);
    }
}



// Called by destination backend to ack the data we sent for writing
void IDmaBeTcdmBanks::write_data_ack(uint8_t *data)
{
    // Release the data since we are now sure it won't be used anymore
    delete[] data;
    // And check if there is any action to take since backend may became ready
    this->update();
}



// This is called everytime we should check if any action should be taken
void IDmaBeTcdmBanks::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaBeTcdmBanks *_this = (IDmaBeTcdmBanks *)__this;

    // Check if we should acknowledge the previous line, this can happen when the write request
    // got a latency
    if (_this->write_ack_timestamp != -1)
    {
        if (_this->write_ack_timestamp <= _this->clock.get_cycles())
        {
            _this->write_ack_timestamp = -1;
            _this->remove_chunk_from_current_burst(_this->write_ack_size);
            _this->write_handle_req_ack();
        }
        else
        {
            _this->fsm_event.enqueue(_this->write_ack_timestamp - _this->clock.get_cycles());
        }
    }

    // If a write chunk is pending, only send a line if we are not waiting for previous line
    // acknowledgement
    if (_this->write_current_chunk_size > 0 && _this->write_ack_timestamp == -1)
    {
        // Pending write chunk
        _this->write_line();
    }

    if (_this->burst_queue_is_write.size() > 0 && !_this->burst_queue_is_write.front())
    {
        // If a read burst is pending, only read new line fi previous one has been sent
        if (_this->current_burst_size > 0 && _this->read_pending_line_size == 0)
        {
            _this->read_line();
        }

        // If a read line is stuck because backend was not ready to receive it,
        // check if it is now ready
        if (_this->read_pending_line_size > 0 && _this->be->is_ready_to_accept_data(_this->burst_queue_transfer.front()))
        {
            // Maybe it was actually stuck due to latency
            if (_this->read_pending_timestamp <= _this->clock.get_cycles())
            {
                uint64_t size = _this->read_pending_line_size;
                _this->read_pending_line_size = 0;
                IdmaTransfer *transfer = _this->burst_queue_transfer.front();
                _this->remove_chunk_from_current_burst(size);
                _this->be->write_data(transfer, _this->read_pending_line_data, size);
            }
            else
            {
                _this->fsm_event.enqueue(_this->read_pending_timestamp - _this->clock.get_cycles());
            }
        }
    }
}



void IDmaBeTcdmBanks::update()
{
    // All the checks are centralized in a new cycle in the FSM
    this->fsm_event.enqueue();
}



bool IDmaBeTcdmBanks::is_empty()
{
    return this->burst_queue_base.empty();
}
