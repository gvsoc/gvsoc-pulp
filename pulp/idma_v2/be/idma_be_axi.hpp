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

#include <deque>
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "../idma.hpp"
#include "idma_be.hpp"

/**
 * @brief AXI backend (IO v2)
 *
 * This backend can be used to interface with any IoReq-based router speaking the
 * v2 IO protocol (vp/itf/io_v2.hpp). It can send several outstanding bursts up to
 * the defined limit, mirroring the HW NumAxInFlight depth (idma_backend.sv).
 */
class IDmaBeAxi : public vp::Block, public IdmaBeConsumer
{
public:
    IDmaBeAxi(vp::Component *idma, std::string itf_name, IdmaBeProducer *be);
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
    // Per-burst metadata for the pre-allocated read burst slots. One entry per
    // slot in `bursts`, paired by index. Replaces the v1 IoReq arg-stack and the
    // v1 `read_timestamps[req->id]` parallel array. Mirrors the per-burst
    // metadata FIFOs (`MetaFifoDepth`-sized) in the HW backend.
    struct BurstInfo
    {
        IdmaTransfer *transfer;
        int64_t       ready_cycle;
    };

    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    static void axi_response(vp::Block *__this, vp::IoReq *req);
    static void axi_retry(vp::Block *__this);
    void read_handle_req_end(vp::IoReq *req);
    void write_handle_req_end(vp::IoReq *req);
    void send_read_burst_to_axi();
    void enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer);

    IdmaBeProducer *be;
    vp::IoMaster ico_itf{&IDmaBeAxi::axi_retry, &IDmaBeAxi::axi_response};
    vp::Trace trace;
    vp::ClockEvent fsm_event;

    // Pre-allocated read burst slots (size = burst_queue_size = HW NumAxInFlight).
    std::vector<vp::IoReq> bursts;
    // Per-slot metadata, parallel to `bursts`. Linked at construction time so
    // every IoReq carries `initiator = &burst_info[i]` permanently.
    std::vector<BurstInfo> burst_info;
    std::queue<vp::IoReq *> free_bursts;

    std::queue<vp::IoReq *> read_waiting_bursts;
    std::queue<vp::IoReq *> read_bursts_waiting_ack;

    // Pending bursts queue. read bursts stay at the head until accepted by the
    // downstream router; write bursts are popped at write_data() time.
    std::queue<vp::IoReq *> pending_bursts;
    std::queue<vp::IoReq *> pending_bursts_ack;

    uint64_t current_burst_base;
    uint64_t current_burst_size;

    int burst_size;

    // v2 deny/retry: when the AXI master gets IO_REQ_DENIED on a send, it
    // suspends issuing until axi_retry() fires. Read bursts are simply left at
    // the head of pending_bursts; write requests (allocated dynamically per
    // chunk) are stashed in denied_writes and replayed in order.
    bool denied_blocked = false;
    std::deque<vp::IoReq *> denied_writes;
};
