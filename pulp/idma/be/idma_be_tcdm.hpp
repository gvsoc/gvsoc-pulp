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

#pragma once

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "../idma.hpp"
#include "idma_be.hpp"

/**
 * @brief TCDM back-end
 *
 * This back-end can be used to interface directly with a local memory.
 * It can only send one request and is blocked until the request is done.
 */
class IDmaBeTcdm : public vp::Block, public IdmaBeConsumer
{
public:
    /**
     * @brief Construct a new TCDM back-end
     *
     * @param idma The top iDMA block.
     * @param itf_name Name of the TCDM interface where the back-end should send requests.
     * @param be The top back-end.
     */
    IDmaBeTcdm(vp::Component *idma, std::string itf_name, IdmaBeProducer *be);

    void reset(bool active) override;

    void update();
    void read_burst(uint64_t base, uint64_t size) override;
    void write_burst(uint64_t base, uint64_t size) override;
    void write_data(uint8_t *data, uint64_t size) override;
    void write_data_ack(uint8_t *data) override;
    uint64_t get_burst_size(uint64_t base, uint64_t size) override;
    bool can_accept_burst() override;
    bool can_accept_data() override;

private:
    // FSM handler, called to check if any action should be taken after something was updated
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // Get the size of the line which can be accessed on TCDM side, to respect the size of
    // the interconnect
    uint64_t get_line_size(uint64_t base, uint64_t size);
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
    void enqueue_burst(uint64_t base, uint64_t size, bool is_write);

    // Pointer to back-end, used for data synchronization
    IdmaBeProducer *be;
    // AXI interface where requests can be posted
    vp::IoMaster ico_itf;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Block FSM event, used to trigger all checks after something has been updated
    vp::ClockEvent fsm_event;
    // Width in bytes of the TCDM interface. Requests to the TCDM will be split to fit
    // this width
    int width;
    // Top property giving the size of the queue of pending bursts
    int burst_queue_maxsize;
    // Top parameter giving base address of local memory
    uint64_t loc_base;

    // Request used for TCDM accesses, only one at the same time is possible
    vp::IoReq req;

    // Queue of pending bursts giving burst base address
    std::queue<uint64_t> burst_queue_base;
    // Queue of pending bursts giving burst size
    std::queue<uint64_t> burst_queue_size;
    // Queue of pending bursts telling if burst is read or write
    std::queue<bool> burst_queue_is_write;

    // Base address of currently active burst, the one from which lines are read or written
    uint64_t current_burst_base;
    // Size of currently active burst, the one from which lines are read or written
    uint64_t current_burst_size;

    // When a chunk is being written line by line, this gives the base address for next line
    uint64_t write_current_chunk_base;
    // When a chunk is being written line by line, this gives the remaining size
    uint64_t write_current_chunk_size;
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
};
