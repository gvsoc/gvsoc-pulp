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

#include <string>
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "idma_manager.hpp"
#include "idma_be.hpp"

/**
 * @brief AXI read manager (idma_axi_read + its AR channel), an io_v2 beat
 * master.
 *
 * - A split becomes one data-less read request of num_beats x width bytes at
 *   the word-aligned address (the RTL AR), issued in the cycle the legalizer
 *   emits the split; a denied request holds the address channel until the
 *   bus retries it.
 * - The response comes back as one beat per bus word, each a distinct
 *   allocator object; a beat is pushed into the back-end's buffer if the
 *   burst heads the read FIFO and the masked lanes have room, else it is
 *   denied and re-offered through resp_retry() once the back-end tick has
 *   applied the cycle's pops (the RTL r_ready).
 * - The page a burst may not cross is 2^(log2(width) + burst_len) bytes,
 *   capped at the AXI 4 KiB page.
 */
class IdmaAxiRead : public vp::Block, public IdmaReadManager
{
public:
    /// @param top              Owning component; the master port is created on
    ///                         it under @p itf_name.
    /// @param itf_name         Port and block name (axi_read).
    /// @param be               Back-end the beats are pushed into.
    /// @param width            Data path width in bytes (the beat size).
    /// @param burst_len        log2 of the burst length in bus words.
    /// @param num_ax_in_flight Outstanding bursts (contexts kept).
    IdmaAxiRead(vp::Component *top, std::string itf_name, IdmaBackend *be, int width,
        int burst_len, int num_ax_in_flight);

    /// Hardware reset: frees every outstanding request.
    void reset(bool active) override;
    /// Checks the bound producer supports response back-pressure.
    void start() override;

    // IdmaReadManager (see idma_manager.hpp)
    int protocol() override { return IDMA_PROT_AXI; }
    bool not_bursting() override { return false; }
    uint32_t bytes_to_page_boundary(uint64_t addr) override;
    bool ar_ready() override;
    void *issue_ar(const IdmaSplit &split) override;
    void retry_held_beat() override;
    bool busy() override;

private:
    /// One outstanding read burst.
    struct ReadCtx
    {
        /// The data-less burst request, ours until the last response beat.
        vp::IoReq *req = nullptr;
        /// Index in the pool, reported as burst_id.
        int slot = 0;
        /// Beats of the burst and beats already received.
        int beats_expected = 0;
        int beats_received = 0;
        bool in_use = false;
    };

    /// Bus retry: re-sends the held burst request.
    static void retry_meth(vp::Block *__this, vp::IoRetryChannel channel);
    /// Response beat of a burst (R channel).
    static vp::IoRespAck resp_meth(vp::Block *__this, vp::IoReq *req);

    /// Offer a response beat to the back-end; returns false when refused.
    bool push_beat(vp::IoReq *beat);
    /// Free a burst context and its request.
    void release(ReadCtx *ctx);

    IdmaBackend *be;
    vp::Trace trace;
    /// The io_v2 beat master (AR + R).
    vp::IoMaster bus;
    /// Pool of data-less requests.
    vp::IoReqAllocator *req_allocator;

    /// Data path width in bytes.
    int width;
    /// Boundary a burst may not cross, in bytes.
    uint64_t page_size;
    /// Burst contexts and the free ones.
    std::vector<ReadCtx> ctxs;
    std::vector<ReadCtx *> free_ctxs;
    /// Request refused by the bus, re-sent on retry.
    vp::IoReq *held_ar = nullptr;
    /// Response beat refused for lack of buffer room, owned by the producer.
    vp::IoReq *held_resp = nullptr;
    /// Scratch bus word the beat payload is aligned into.
    std::vector<uint8_t> bus_word;
};
