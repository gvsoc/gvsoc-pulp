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
#include <vp/vp.hpp>
#include "../idma.hpp"
#include "../be/idma_fifo.hpp"

/**
 * @brief Request FIFO and ND mid-end (stream_fifo + idma_nd_midend).
 *
 * Front-ends push ND transfers into a registered FIFO of req_fifo_depth
 * entries; the back-end pulls 1D requests out of the splitter, one per
 * cycle:
 *
 * - dimension 2 adds the second strides after every 1D, dimension 3 adds the
 *   third strides instead on the wrap of dimension 2 (idma_nd_midend keeps
 *   one address per dimension and reloads the lower ones); a repetition
 *   count of 0 means 1;
 * - the last 1D of the transfer carries super_last; its completion (or the
 *   rejection of a zero-length 1D, which the back-end flags last) completes
 *   the ND transfer towards the front-end;
 * - an ND transfer leaves the FIFO when its last 1D is handed over, and the
 *   slot is usable one cycle later (registered FIFO), which is when a denied
 *   launch is retried;
 * - the pull model needs no per-cycle ClockEvent: the legalizer asks once per
 *   cycle.
 *
 * Instances are named (me, me0, me1) so a multi-stream DMA has one per stream.
 */
class IdmaMeNd : public vp::Block, public IdmaFeSink, public IdmaMeSource
{
public:
    /// @param top        Owning component (the block is created as its child).
    /// @param name       Block name (me, me0, me1).
    /// @param fifo_depth Depth of the request FIFO.
    /// @param nb_dims    Dimensions iterated (1 to 3); higher ones are ignored.
    /// @param fe         The front-end's side of this stream.
    IdmaMeNd(vp::Component *top, std::string name, int fifo_depth, int nb_dims,
        IdmaFeSource *fe);

    /// Back-end to wake when a request is pushed (once, at construction).
    void set_backend(IdmaBeWake *be) { this->be = be; }

    /// Hardware reset: empties the FIFO and drops the request being split.
    void reset(bool active) override;

    // IdmaFeSink (see idma.hpp)
    bool can_accept_nd() override;
    void push_nd(IdmaNdReq *req, int extra_delay) override;
    bool is_busy() override;

    // IdmaMeSource (see idma.hpp)
    Idma1dReq *peek_1d(int64_t now) override;
    void take_1d(int64_t now) override;
    void complete_1d(Idma1dReq *req, bool force_last) override;

private:
    /// Signals me_ready() to the front-end one cycle after a FIFO pop.
    static void ready_handler(vp::Block *__this, vp::ClockEvent *event);

    /// Start splitting the ND request heading the FIFO.
    void load(IdmaNdReq *req);
    /// Build the next 1D of the current request (cursor state).
    Idma1dReq *build_1d();

    /// The front-end's side of the stream, and the back-end to wake.
    IdmaFeSource *fe;
    IdmaBeWake *be = nullptr;
    vp::Trace trace;
    /// Signals the front-end one cycle after a FIFO slot was freed.
    vp::ClockEvent ready_event;

    /// Dimensions iterated.
    int nb_dims;
    /// The request FIFO; the request being split stays at its head.
    IdmaTimedFifo<IdmaNdReq *> queue;

    /// ND request being split and its cursors: next line addresses, the
    /// repetition counts of dimensions 2 and 3, and the current indexes.
    IdmaNdReq *current = nullptr;
    uint64_t src;
    uint64_t dst;
    uint64_t reps[2];
    uint64_t idx[2];
    /// 1D built for the pending peek, not yet taken.
    Idma1dReq *pending = nullptr;
    /// Current cycle of the block's clock domain.
    int64_t cycles() { return this->clock.get_cycles(); }
};
