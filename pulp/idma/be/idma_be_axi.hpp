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

#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "../idma.hpp"
#include "idma_be.hpp"

/**
 * @brief AXI backend
 *
 * This backend can be used to interface with any IoReq-based router.
 * It can send several outstanding bursts, up to the defined limit so that it correctly
 * models the timing behavior created by the router latency.
 */
class IDmaBeAxi : public vp::Block, public IdmaBeConsumer
{
public:
    /**
     * @brief Construct a new AXI backend
     *
     * @param idma The top iDMA block.
     * @param itf_name Name of the AXI interface where the backend should send requests.
     * @param be The top backend.
     */
    IDmaBeAxi(vp::Component *idma, std::string itf_name, IdmaBeProducer *be);

    /**
     * @brief Destroy an AXI backend
     */
    ~IDmaBeAxi();

    void reset(bool active) override;

    void update();
    void read_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size) override;
    void write_burst(IdmaTransfer *transfer, uint64_t base, uint64_t size) override;
    void write_data_ack(uint8_t *data) override;
    void write_data(IdmaTransfer *transfer, uint8_t *data, uint64_t size) override;
    uint64_t get_burst_size(uint64_t base, uint64_t size) override;
    bool can_accept_burst() override;
    bool can_accept_data() override;
    bool is_empty() override;

private:
    // FSM handler, called to check if any action should be taken after something was updated
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // Called when an asynchronous response is received from AXI
    static void axi_response(vp::Block *__this, vp::IoReq *req);
    // Once a read burst is finished, it can be enqueued with this function so that it is
    // notified after the latency has elapsed
    void read_handle_req_end(vp::IoReq *req);
    // Called when a write requests is finish to handle it
    void write_handle_req_end(vp::IoReq *req);
    // Send the pending read burst to AXI
    void send_read_burst_to_axi();
    // Enqueue a burst to pending queue. Burst will be processed in order
    void enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer);

    // Pointer to backend, used for data synchronization
    IdmaBeProducer *be;
    // AXI interface where requests can be posted
    vp::IoMaster ico_itf;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Block FSM event, used to trigger all checks after something has been updated
    vp::ClockEvent fsm_event;
    // Array of statically allocated bursts, according to specified number of outstanding
    // AXI requests
    std::vector<vp::IoReq> bursts;
    // Available bursts. Bursts can be allocated until this queue is empty
    std::queue<vp::IoReq *> free_bursts;

    // Bursts which received a response but are waiting proper timestamp to take into
    // account the returned latency
    std::queue<vp::IoReq *> read_waiting_bursts;
    // Bursts sent to destination backend are enqueued here until they are acknowledged and
    // released
    std::queue<vp::IoReq *> read_bursts_waiting_ack;
    // List of timestamps for each burst where they can be considered as finished
    std::vector<int64_t> read_timestamps;

    // Queue of pending bursts. This contains both read and write bursts. This is mostly used
    // to process them in order. The front burst is removed from the queue once it is fully
    // processed.
    std::queue<vp::IoReq *> pending_bursts;

    std::queue<vp::IoReq *> pending_bursts_ack;

    // Current base of the first transfer. This is when a chunk of data to be written is received
    // to know the base where it should be written.
    uint64_t current_burst_base;
    uint64_t current_burst_size;
};
