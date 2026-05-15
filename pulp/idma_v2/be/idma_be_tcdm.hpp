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
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "../idma.hpp"
#include "idma_be.hpp"

/**
 * @brief TCDM back-end (IO v2)
 *
 * This back-end interfaces directly with a local memory through the v2 IO
 * protocol. Only one request is in flight at a time.
 */
class IDmaBeTcdm : public vp::Block, public IdmaBeConsumer
{
public:
    IDmaBeTcdm(vp::Component *idma, std::string itf_name, IdmaBeProducer *be);

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
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    static void tcdm_response(vp::Block *__this, vp::IoReq *req);
    static void tcdm_retry(vp::Block *__this);
    uint64_t get_line_size(uint64_t base, uint64_t size);
    void write_line();
    void read_line();
    void write_handle_req_ack();
    void remove_chunk_from_current_burst(uint64_t size);
    void activate_burst();
    void enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer);
    // Handle a synchronous DONE for a write line: latency may be 0 (handle
    // inline) or > 0 (defer via the existing write_ack_timestamp machinery).
    void write_complete_sync(int64_t latency, uint64_t size);
    // Handle a synchronous DONE for a read line. `transfer` is captured at
    // the caller (read_line / tcdm_response) BEFORE remove_chunk_from_current_burst()
    // potentially pops it off burst_queue_transfer.
    void read_complete_sync(int64_t latency, uint64_t size, IdmaTransfer *transfer);

    IdmaBeProducer *be;
    vp::IoMaster ico_itf{&IDmaBeTcdm::tcdm_retry, &IDmaBeTcdm::tcdm_response};
    vp::Trace trace;
    vp::ClockEvent fsm_event;
    int width;
    int burst_queue_maxsize;
    uint64_t loc_base;

    vp::IoReq req;

    std::queue<uint64_t> burst_queue_base;
    std::queue<uint64_t> burst_queue_size;
    std::queue<bool> burst_queue_is_write;
    std::queue<IdmaTransfer *> burst_queue_transfer;

    uint64_t current_burst_base;
    uint64_t current_burst_size;

    uint64_t write_current_chunk_base;
    uint64_t write_current_chunk_size;
    uint64_t write_current_chunk_ack_size;
    uint8_t *write_current_chunk_data;
    uint8_t *write_current_chunk_data_start;

    // FIFO of write-chunks whose lines have been issued on the TCDM bus but
    // whose ack to the source has not yet fired. Each entry represents one
    // chunk (i.e. one ack_data() call to the source) — for multi-line chunks
    // the entry is pushed only when the last line is issued, with
    // `cycle = T_last_line_issue + latency`. Decoupling the ack queue from
    // line issuance lets TCDM accept one new chunk every cycle even though
    // the per-line latency may be several cycles — latency only delays the
    // ack, it does not block the bus.
    struct PendingAck
    {
        IdmaTransfer *transfer;
        uint8_t      *data;
        uint64_t      size;
        int64_t       cycle;
    };
    std::deque<PendingAck> write_pending_acks;
    // Cap on simultaneously-pending chunk acks. Anything bigger than the
    // central BE will ever push at once is fine; 16 is plenty for the
    // 4 KiB-burst / 8 B-line case where the bandwidth router latency is
    // ~2 cycles.
    int write_pending_acks_max = 16;

    // FIFO of read lines issued on the TCDM bus but not yet handed to the
    // destination BE. Pipelines reads at 1 line per cycle: while one line's
    // latency window is still elapsing, more lines can be issued behind it.
    // Drained in order in fsm_handler, paced by `ready_cycle` and by the
    // destination BE's readiness.
    struct PendingRead
    {
        IdmaTransfer *transfer;
        uint8_t      *data;
        uint64_t      size;
        int64_t       ready_cycle;
    };
    std::deque<PendingRead> read_pending_pushes;
    int read_pending_pushes_max = 16;

    int64_t last_line_timestamp;
    IdmaTransfer *write_current_transfer;

    // v2 deny/retry / async resp: if the router denies a line, we leave the
    // current chunk pointers untouched and wait for tcdm_retry() to clear
    // denied_blocked. If the router accepts but defers (IO_REQ_GRANTED),
    // granted_blocked is set until tcdm_response() fires; the in-flight line
    // size (and read buffer) live in pending_line_*.
    bool denied_blocked = false;
    bool granted_blocked = false;
    bool pending_line_is_write = false;
    uint64_t pending_line_size = 0;
    uint8_t *pending_line_data = nullptr;
};
