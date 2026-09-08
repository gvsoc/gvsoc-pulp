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

#pragma once

#include <vp/vp.hpp>
#include <vector>
#include <vp/itf/io.hpp>
#include "../idma.hpp"
#include "idma_be.hpp"

/**
 * @brief Bank-interleaved TCDM back-end
 *
 * Same model as IDmaBeTcdm, but the local interface is split over several ports, the way
 * mem_to_banks does in the RTL wrapper: one access of up to nb_banks * width bytes is broken into
 * one word-interleaved access per bank, all issued in the same cycle and completing together, so
 * the channel moves nb_banks words per cycle instead of one.
 *
 * The pulp_cluster wrapper instantiates this with two banks per direction, giving the four
 * tcdm_master ports of dmac_wrap.
 *
 * Like IDmaBeTcdm, a single access is in flight at a time and responses are synchronous.
 */
class IDmaBeTcdmBanks : public vp::Block, public IdmaBeConsumer
{
public:
    /**
     * @brief Construct a new TCDM back-end
     *
     * @param idma The top iDMA block.
     * @param itf_name Name of the TCDM interface where the back-end should send requests.
     * @param be The top back-end.
     */
    IDmaBeTcdmBanks(vp::Component *idma, std::string itf_name, IdmaBeProducer *be);

    void reset(bool active) override;

    void update();
    void read_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size) override;
    void write_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size) override;
    void write_data(IdmaTransfer *transfer, uint8_t *data, uint64_t size) override;
    void write_data_ack(uint8_t *data) override;
    uint64_t get_burst_size(uint64_t base, uint64_t size) override;
    bool can_accept_burst() override;
    bool can_accept_data() override;
    bool is_empty() override;

private:
    // FSM handler, called to check if any action should be taken after something was updated
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // Get the size of the line which can be accessed on TCDM side, to respect the size of
    // the interconnect
    uint64_t get_line_size(uint64_t base, uint64_t size);
    // Issue one line as one access per covered bank, all in the same cycle. Returns the latency
    // of the slowest bank, since the banks are accessed in parallel and complete together.
    int64_t access_banks(uint64_t base, uint64_t size, uint8_t *data, bool is_write);
    // Write a line to TCDM
    void write_line();
    // Read a line from TCDM
    void read_line();
    // Handle the end of a write request
    void write_handle_req_ack();
    // Remove a chunk of data from current burst. This is used to track when a burst is done
    void remove_chunk_from_current_burst(uint64_t size);
    // Extract first pending information to let FSM start writing and reading lines from it
    void activate_burst();
    // Enqueue a new burst to the queue of pending bursts
    void enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer);

    // Pointer to back-end, used for data synchronization
    IdmaBeProducer *be;
    // One interface per bank, requests are spread over them by word interleaving
    std::vector<vp::IoMaster *> ico_itf;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Block FSM event, used to trigger all checks after something has been updated
    vp::ClockEvent fsm_event;
    // Width in bytes of one bank port. Accesses to a bank are split to fit this width
    int width;
    // Number of bank ports the local interface is split over
    int nb_banks;
    // Top property giving the size of the queue of pending bursts
    int burst_queue_maxsize;
    // Top parameter giving base address of local memory
    uint64_t loc_base;

    // Requests used for TCDM accesses, one per bank since they are issued in the same cycle
    std::vector<vp::IoReq> reqs;

    // Queue of pending bursts giving burst base address
    std::queue<uint64_t> burst_queue_base;
    // Queue of pending bursts giving burst size
    std::queue<uint64_t> burst_queue_size;
    // Queue of pending bursts telling if burst is read or write
    std::queue<bool> burst_queue_is_write;
    // Queue of transfers associated to queue of bursts
    std::queue<IdmaTransfer *> burst_queue_transfer;

    // Base address of currently active burst, the one from which lines are read or written
    uint64_t current_burst_base;
    // Size of currently active burst, the one from which lines are read or written
    uint64_t current_burst_size;

    // When a chunk is being written line by line, this gives the base address for next line
    uint64_t write_current_chunk_base;
    // When a chunk is being written line by line, this gives the remaining size
    uint64_t write_current_chunk_size;
    // Size to be acknowledged when write response is received
    uint64_t write_current_chunk_ack_size;
    // When a chunk is being written line by line, this gives the data pointer for next line
    uint8_t *write_current_chunk_data;
    // When a chunk is being written line by line, this gives the data pointer to the beginning
    // of the chunk
    uint8_t *write_current_chunk_data_start;
    // Once data is written, this is set to the timestamp where the data must be acknowledged,
    // according to the latency reported by the interconnect
    int64_t write_ack_timestamp;
    // Size of the chunk to be acknowledged when timestamp is reached, used to update the burst
    uint64_t write_ack_size;

    // When a burst is being read, this gives the timestamp for the next line, nothing is
    // read before this timestamp is reached, used to take into account previous request
    // latency
    int64_t read_pending_timestamp;
    // When a burst is being read, the last line read from TCDM may be blocked because the
    // backend is not ready to accept it. In this case this gives the data pointer containing the
    // data to be written
    uint8_t *read_pending_line_data;
    // When a burst is being read, the last line read from TCDM may be blocked because the
    // backend is not ready to accept it. In this case this gives the size of the
    // data to be written
    uint64_t read_pending_line_size;
    // Timestamp in cycles of the last time a line was read or written. Used to make sure we send
    // only one line per cycle
    int64_t last_line_timestamp;
    // Current transfer for which data to be written are push
    IdmaTransfer *write_current_transfer;
};
