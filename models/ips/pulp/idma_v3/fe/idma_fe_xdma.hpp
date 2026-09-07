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

#include <vp/vp.hpp>
#include <cpu/iss/include/offload.hpp>
#include <vp/itf/wire.hpp>
#include <vp/register.hpp>
#include <vp/signal.hpp>
#include "../idma.hpp"

class IdmaBackend;

/**
 * @brief Snitch xdma front-end (axi_dma_tc_snitch_fe): the core offloads
 * the dmsrc / dmdst / dmstr / dmrep / dmcpy / dmstat instructions through a
 * synchronous wire.
 *
 * - dmcpy(i) allocates the transfer id and pushes the transfer into the
 *   request FIFO; when the FIFO is full the instruction is not granted, the
 *   core stalls (replaying the instruction, which is refused again) and the
 *   grant comes back on the offload_grant wire once a slot is free; the
 *   replay that follows the grant is answered with the id already
 *   allocated. The decision is taken inside the offload call, as the
 *   iss_v2 core requires.
 * - dmstat(i) returns, at once, the completed id (0), the next id (1), the
 *   busy flag (2) or whether a dmcpy would block (3).
 * - cfg bit 0 decouples the read and write sides, bit 1 selects 2D.
 * - completed_id is updated the cycle after the transfer completes
 *   (registered counter), and the completion pulses ``irq``.
 */
class IdmaFeXdma : public vp::Block, public IdmaFeSource
{
public:
    /// @param top Owning component; the offload, offload_grant, irq and busy
    ///            ports are created on it.
    /// @param me  Mid-end the transfers are pushed into.
    IdmaFeXdma(vp::Component *top, IdmaFeSink *me);

    /// Hardware reset: drops a stalled transfer.
    void reset(bool active) override;

    /// Back-end of the stream, for the busy wire (once, at construction).
    void set_backend(IdmaBackend *be) { this->be = be; }

    // IdmaFeSource
    void me_ready() override;
    void complete_nd(IdmaNdReq *req) override;

private:
    /// An xdma instruction offloaded by the core (decode on func7).
    static void offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    /// Applies the pending completions to completed_id and pulses irq.
    static void done_handler(vp::Block *__this, vp::ClockEvent *event);
    /// dmcpy: allocate the id and enqueue, or stall; returns the id.
    uint32_t enqueue_copy(uint32_t config, uint32_t size, bool &granted);
    /// dmstat: completed id (0), next id (1), busy (2), would block (3).
    uint32_t get_status(uint32_t status);
    /// Recompute and drive the busy wire.
    void update_busy();

    IdmaFeSink *me;
    IdmaBackend *be = nullptr;
    vp::Trace trace;
    /// The core's offload wire and the grant wire back to it.
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf;
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf;
    /// Completion pulse and busy wire.
    vp::WireMaster<bool> irq_itf;
    vp::WireMaster<bool> busy_itf;
    /// completed_id update and irq pulse, one cycle after the completions.
    vp::ClockEvent done_event;

    /// The transfer registers set by dmsrc / dmdst / dmstr / dmrep.
    vp::Register<uint64_t> src;
    vp::Register<uint64_t> dst;
    vp::Register<uint64_t> src_stride;
    vp::Register<uint64_t> dst_stride;
    vp::Register<uint32_t> reps;
    /// Id of the next dmcpy (reset 2) and completed id (reset 1).
    vp::Register<uint32_t> next_transfer_id;
    vp::Register<uint32_t> completed_id;
    /// GUI signal: a transfer is in flight.
    vp::Signal<bool> trace_busy;

    /// Transfer not granted for lack of FIFO room, pushed on me_ready().
    IdmaNdReq *stalled = nullptr;
    /// The core replays the granted dmcpy: answer it with this id.
    bool grant_replay = false;
    uint32_t grant_id = 0;
    /// Completions of the current cycle, applied next cycle.
    int pending_done = 0;
    /// Last value driven on the busy wire.
    bool busy = false;
};
