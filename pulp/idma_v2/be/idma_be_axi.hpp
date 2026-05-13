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
 * @brief AXI backend (IO v2, beat-streaming)
 *
 * Mirrors the HW AXI backend (idma_axi_read.sv / idma_axi_write.sv): a logical
 * burst is split into a stream of width-sized beats, exchanged one beat per
 * cycle on the bus. In v2 IO terms each beat is its own `IoReq`, all beats of
 * a burst share one `burst_id`, the first carries `is_first=true`, the last
 * `is_last=true`. The bus width is taken from `axi_width` (bytes).
 *
 * Each burst slot owns a 4 KB data buffer (the burst staging area, sized to
 * one AXI page) and a vector of pre-allocated beat `IoReq` objects whose data
 * pointers slice into that buffer. The slot stays alive until every beat has
 * been responded to.
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
    // Per-burst state. One BurstInfo + one data buffer + one beat pool per slot,
    // all pre-allocated at construction time. Slots are dispatched via free_bursts
    // and recycled when bytes_responded reaches total_size.
    struct BurstInfo
    {
        IdmaTransfer *transfer = nullptr;
        // Burst geometry (bytes).
        uint64_t base = 0;
        uint64_t total_size = 0;
        // Cursors. bytes_buffered is only used by writes.
        uint64_t bytes_buffered = 0;
        uint64_t bytes_issued = 0;
        uint64_t bytes_responded = 0;
        // Read-only: bytes already handed to the destination BE as
        // write_data() chunks. Lags bytes_responded by however many beat
        // responses haven't been forwarded yet (chiefly because the
        // destination BE wasn't ready).
        uint64_t bytes_pushed = 0;
        // Read-only: bytes the destination BE has acknowledged via
        // write_data_ack(). When bytes_acked reaches total_size the slot is
        // free.
        uint64_t bytes_acked = 0;
        // Per-beat earliest-forward times (for reads only). Pushed by
        // handle_beat_resp() in the order beats respond; popped by the FSM
        // when the corresponding chunk has been handed to the destination
        // BE. Decouples per-beat latency from the slot-wide cursor.
        std::deque<int64_t> beat_ready_cycles;
        // Source-side chunks received via write_data() but not yet
        // acknowledged. The destination BE owes the source one
        // ack_data(transfer, data, size) per entry; we issue those only
        // after the corresponding write beats have been responded so the
        // source doesn't see the write complete before it actually has.
        std::deque<std::pair<uint8_t *, uint64_t>> write_pending_acks;
        // Sum of sizes in write_pending_acks already covered by responses
        // (used so we can walk write_pending_acks and ack one entry once
        // bytes_responded crosses its end boundary).
        uint64_t write_bytes_source_acked = 0;
        // Unique tag carried by every beat of this burst on the io_v2 master.
        // We use the slot index so it is unique and stable.
        int64_t burst_id = -1;
        // Where the next free beat IoReq sits inside `beats`. Increments
        // monotonically as issue_beat() consumes them; reset when the slot is
        // freed.
        int next_beat_idx = 0;
        // Pre-allocated beat pool. Data pointers slice into the slot's buffer.
        std::vector<vp::IoReq> beats;
        bool is_write = false;
    };

    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    static void axi_response(vp::Block *__this, vp::IoReq *req);
    static void axi_retry(vp::Block *__this);

    // Allocate the next free slot, initialise it for a new burst, and queue it
    // for issue (and, for writes, for filling).
    void enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer);
    // Send one beat from the head of `pending_bursts`, paced at one beat per
    // cycle. Returns true when a beat was actually issued (caller reschedules
    // the FSM for next cycle).
    bool issue_beat();
    // Common path for "a beat just returned": accumulate latency, advance
    // bytes_responded, finalize write bursts when the last write beat lands,
    // and gate the read-side downstream push by per-beat latency.
    void handle_beat_resp(BurstInfo *info, uint64_t size, int64_t latency);

    IdmaBeProducer *be;
    vp::IoMaster ico_itf{&IDmaBeAxi::axi_retry, &IDmaBeAxi::axi_response};
    vp::Trace trace;
    vp::ClockEvent fsm_event;

    // Configuration.
    int axi_width;           // beat size in bytes
    int burst_size;          // optional cap on a logical burst size (0 = no cap)
    int burst_queue_size;    // number of in-flight bursts (slot count)

    // Slot pool: one BurstInfo per slot. Indexed by slot id, which is also the
    // burst_id we tag every beat with.
    std::vector<BurstInfo *> burst_info;
    // Pre-allocated burst data buffers (one per slot, AXI_PAGE_SIZE bytes).
    std::vector<uint8_t *> burst_data;
    // Free slots.
    std::queue<BurstInfo *> free_bursts;
    // Bursts whose beats have not all been issued yet. Reads stay here until
    // every beat has been sent; writes stay here while the FSM is draining
    // buffered bytes into beats.
    std::queue<BurstInfo *> pending_bursts;
    // Write-side fill queue: bursts whose buffer is being filled by
    // write_data(). Front-most slot is the current fill target.
    std::queue<BurstInfo *> write_fill_queue;
    // Read bursts whose first beat has arrived and need to forward chunks
    // downstream as further beats arrive. A slot leaves this queue when
    // bytes_pushed reaches total_size; the slot itself stays alive until
    // bytes_acked also reaches total_size.
    std::queue<BurstInfo *> read_push_queue;
    // FIFO of (slot, chunk-size) entries describing chunks currently in
    // flight downstream. Each write_data_ack() pops the head and charges its
    // size to the slot's bytes_acked.
    std::queue<std::pair<BurstInfo *, uint64_t>> read_ack_queue;

    // v2 deny/retry: when the AXI master gets IO_REQ_DENIED on a send, it
    // suspends issuing until axi_retry() fires.
    bool denied_blocked = false;
};
