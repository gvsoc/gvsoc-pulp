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
#include <queue>
#include <tuple>
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "../idma.hpp"
#include "idma_be.hpp"

/**
 * @brief AXI backend (IO v2, beat-streaming mode)
 *
 * Wire shapes:
 *   - Reads:  one IoReq per logical AXI burst with size = total_burst_bytes,
 *             is_first = is_last = true. The slave responds in any of the
 *             three io_v2 forms. The framework auto-inserts an
 *             IoV2BeatAdapter on the downstream when the slave's signature is
 *             IoV2BigPacket; that adapter normalises the response into one
 *             resp() per axi_width-sized beat, paced at one beat per cycle.
 *   - Writes: N IoReqs per burst, size = axi_width per beat (with the tail
 *             possibly smaller), is_first / is_last / burst_id set on each.
 *             Each write beat receives a single resp() back.
 *
 * Per-burst state lives in BurstInfo slots; the slot stays alive until every
 * beat has been responded to (writes) or until the destination BE has
 * acknowledged every chunk pushed downstream (reads).
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
    // and recycled when their completion condition is met:
    //   - writes: bytes_responded reaches total_size (all per-beat acks received)
    //   - reads:  bytes_acked reaches total_size (destination BE acked every
    //             forwarded chunk)
    struct BurstInfo
    {
        IdmaTransfer *transfer = nullptr;
        // Burst geometry (bytes).
        uint64_t base = 0;
        uint64_t total_size = 0;
        // Cursors. bytes_buffered is only used by writes (filled by write_data
        // before the bus has a chance to consume it).
        uint64_t bytes_buffered = 0;
        uint64_t bytes_issued = 0;
        uint64_t bytes_responded = 0;
        // Bytes the destination BE has acknowledged via write_data_ack(). Only
        // used by reads; the slot is recycled when bytes_acked == total_size.
        uint64_t bytes_acked = 0;
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
        // Pre-allocated beat pool used by writes (one slot per cycle).
        std::vector<vp::IoReq> beats;
        // Read request: a single full-size IoReq per burst, heap-allocated so
        // the consuming side (slave or auto-inserted beat adapter) owns and
        // frees it like any other request. Held here only across a DENIED retry;
        // cleared to nullptr once the bus accepts it (ownership handed off).
        vp::IoReq *read_req = nullptr;
        bool is_write = false;
    };

    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // io_v2 master callbacks. Fired once per response beat by either the
    // downstream IoV2BigPacket slave (after the auto-inserted adapter has
    // normalised) or by a directly-bound IoV2Beat slave.
    static vp::IoRespAck resp_meth(vp::Block *__this, vp::IoReq *req);
    static void retry_meth(vp::Block *__this, vp::IoRetryChannel);

    // Allocate the next free slot, initialise it for a new burst, and queue it
    // for issue (and, for writes, for filling).
    void enqueue_burst(uint64_t base, uint64_t size, bool is_write, IdmaTransfer *transfer);
    // Send one beat from the head of `pending_bursts`. Reads emit exactly one
    // full-size req per burst; writes emit one axi_width-sized beat per cycle.
    // Returns true when a beat was actually issued (caller reschedules the
    // FSM for next cycle).
    bool issue_beat();

    IdmaBeProducer *be;
    // Bus-facing IoMaster. Declared with signature IoV2Beat on the Python
    // generator; the framework auto-inserts an IoV2BeatAdapter downstream
    // when the bound slave's signature is IoV2BigPacket.
    vp::IoMaster bus;
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
    // Bursts whose beats have not all been issued yet. Reads leave this queue
    // immediately after the (single) read req is accepted; writes stay until
    // the last write beat has been forwarded.
    std::queue<BurstInfo *> pending_bursts;
    // Write-side fill queue: bursts whose buffer is being filled by
    // write_data(). Front-most slot is the current fill target.
    std::queue<BurstInfo *> write_fill_queue;
    // Per-beat read chunks waiting to be forwarded to the destination BE.
    // Pushed in order by resp_meth (already paced at 1/cycle by the adapter)
    // and drained at the destination BE's accept rate by the FSM. One tuple
    // per beat: (slot, data slice, size).
    std::queue<std::tuple<BurstInfo *, uint8_t *, uint64_t>> read_push_queue;
    // FIFO of (slot, chunk-size) entries describing chunks currently in
    // flight downstream. Each write_data_ack() pops the head and charges its
    // size to the slot's bytes_acked.
    std::queue<std::pair<BurstInfo *, uint64_t>> read_ack_queue;

    // v2 deny/retry: when the AXI master gets IO_REQ_DENIED on a send, it
    // suspends issuing until retry_meth() fires.
    bool denied_blocked = false;
};
