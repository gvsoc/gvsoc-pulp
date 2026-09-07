/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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
 * Authors: Germain Haugou (germain.haugou@gmail.com)
 */

#pragma once

#include <deque>
#include <string>
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "idma_manager.hpp"
#include "idma_be.hpp"

/**
 * @brief AXI write manager (idma_axi_write + its AW channel, optionally the
 * idma_channel_coupler), an io_v2 beat master.
 *
 * - A split is one write burst: its address phase is free in io_v2 (a write
 *   burst starts with its first data beat), so without the coupler the AW
 *   is considered issued at once. With the RAW coupler (Spatz) the AW waits
 *   for the first response beat of a read burst (one credit per read burst
 *   whose request was not decoupled), and at most num_ax_in_flight AWs
 *   wait.
 * - Data beats: one per cycle when the buffer holds every byte the beat
 *   strobes; the beat covers the contiguous strobed run, carries
 *   is_first / is_last / burst_id and the same initiator for the whole
 *   burst; the target owns granted beats, a denied beat is held and re-sent
 *   on retry. Data is staged per burst so it lives until the burst ack.
 * - The burst ack (one data-less last beat, or an inline done on the last
 *   beat) is the B response: it retires the burst in the back-end.
 */
class IdmaAxiWrite : public vp::Block, public IdmaWriteManager
{
public:
    /// @param top              Owning component; the master port is created on
    ///                         it under @p itf_name.
    /// @param itf_name         Port and block name (axi_write).
    /// @param be               Back-end the data beats are popped from.
    /// @param width            Data path width in bytes (the beat size).
    /// @param burst_len        log2 of the burst length in bus words.
    /// @param num_ax_in_flight Depth of the coupler's AW store.
    /// @param meta_fifo_depth  Bursts alive from AW to B (contexts kept); 0
    ///                         derives it from num_ax_in_flight.
    /// @param raw_coupling     Insert the read/write channel coupler.
    IdmaAxiWrite(vp::Component *top, std::string itf_name, IdmaBackend *be, int width,
        int burst_len, int num_ax_in_flight, int meta_fifo_depth, bool raw_coupling);

    /// Hardware reset: frees every burst context and held beat.
    void reset(bool active) override;

    // IdmaWriteManager (see idma_manager.hpp)
    int protocol() override { return IDMA_PROT_AXI; }
    bool not_bursting() override { return false; }
    uint32_t bytes_to_page_boundary(uint64_t addr) override;
    bool aw_ready() override;
    void issue_aw(const IdmaSplit &split) override;
    bool tick(int64_t now) override;
    void on_read_first_beat() override;
    bool busy() override;

private:
    /// One write burst, alive from its AW to its response.
    struct WriteCtx
    {
        /// Staged data of the burst, one bus word per beat; the data beats
        /// point into it, so it lives until the burst is acknowledged.
        std::vector<uint8_t> stage;
        /// Index in the pool, reported as burst_id.
        int slot = 0;
        bool in_use = false;
        /// AW sent (always true without the coupler).
        bool aw_issued = false;
        /// Beats sent so far and beats of the burst.
        int beats_sent = 0;
        int num_beats = 0;
        /// A beat of the burst was refused inline as invalid.
        bool error = false;
    };

    /// Bus retry: re-sends the held data beat.
    static void retry_meth(vp::Block *__this, vp::IoRetryChannel channel);
    /// Burst acknowledgement (the data-less last beat, B channel).
    static vp::IoRespAck resp_meth(vp::Block *__this, vp::IoReq *req);

    /// Send a prepared beat; returns false when the bus denied it.
    bool send_beat(vp::IoReq *beat);
    /// The burst of this context is acknowledged.
    void complete(WriteCtx *ctx, vp::IoRespStatus status);

    IdmaBackend *be;
    vp::Trace trace;
    /// The io_v2 beat master (AW + W + B).
    vp::IoMaster bus;
    /// Pool of size-0 requests the beats are taken from.
    vp::IoReqAllocator *req_allocator;

    /// Data path width in bytes.
    int width;
    /// Boundary a burst may not cross, in bytes.
    uint64_t page_size;
    /// Depth of the coupler's AW store.
    int num_ax_in_flight;
    /// The read/write channel coupler is present.
    bool raw_coupling;

    /// Burst contexts and the free ones.
    std::vector<WriteCtx> ctxs;
    std::vector<WriteCtx *> free_ctxs;
    /// Bursts in issue order whose data beats are not all sent.
    std::deque<WriteCtx *> data_queue;
    /// Coupler: AWs waiting for a read credit, and unused credits.
    std::deque<WriteCtx *> aw_waiting;
    int aw_credits = 0;
    /// Data beat refused by the bus, re-sent on retry, and its burst.
    vp::IoReq *held_beat = nullptr;
    WriteCtx *held_ctx = nullptr;
};
