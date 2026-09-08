/*
 * Copyright (C) 2026 Fondazione Chips-IT
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
 * Authors: Lorenzo Zuolo, Fondazione Chips-IT (lorenzo.zuolo@chips.it)
 */

#pragma once

#include <queue>
#include <vp/vp.hpp>
#include <vp/register.hpp>
#include "../idma.hpp"
#include "../idma_nd.hpp"


// Middle-end FSM state, exposed as a trace event ("me_state") for profiling.
enum IDmaMe3dState
{
    ME3D_IDLE,        // no current transfer being decomposed
    ME3D_DECOMPOSING, // splitting the current transfer into bursts for the backend
};


/**
 * @brief 3D middle-end
 *
 * Superset of the 2D middle-end: it decomposes a transfer into bursts following up to three
 * dimensions, matching the idma_nd_midend instantiated by pulp_cluster with the reg32_3d
 * front-end.
 *
 * The innermost dimension is the contiguous line of IdmaTransfer::size bytes. The second
 * dimension repeats it IdmaTransfer::reps times, adding src_stride / dst_stride between lines.
 * The third dimension repeats the whole 2D page IdmaTransfer::data[IDMA_ND_REPS_3] times.
 *
 * Addresses accumulate, exactly as idma_nd_midend does: the stride of a dimension is added to the
 * current address, not to the base of the block being repeated. So the first line of the next page
 * sits at the last line of the current one plus the third-dimension stride, which works out to
 * (reps - 1) * stride_2 + stride_3 per page.
 *
 * Which dimensions are active is given by IdmaTransfer::config, see IDMA_CONFIG_*. A 1D transfer
 * is handled as a 2D transfer with a single repetition, and a 2D transfer as a 3D transfer with a
 * single page, so a single control path covers the three cases.
 */
class IDmaMe3d : public vp::Block, public IdmaTransferConsumer, public IdmaTransferProducer
{
public:
    /**
     * @brief Construct a new IDmaMe3d middle-end
     *
     * @param idma The top iDMA block.
     * @param fe The front end.
     * @param be The back end.
     */
    IDmaMe3d(vp::Component *idma, IdmaTransferProducer *fe, IdmaTransferConsumer *be);

    void reset(bool active) override;

    bool can_accept_transfer() override;
    void enqueue_transfer(IdmaTransfer *transfer) override;
    void update() override;
    void ack_transfer(IdmaTransfer *transfer) override;

private:
    // FSM handler, called to check if any action should be taken after something was updated
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    // Pointer to frontend
    IdmaTransferProducer *fe;
    // Pointer to backend
    IdmaTransferConsumer *be;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Top parameter giving the maximum number of transfers which can be enqueued
    int transfer_queue_size;
    // Queue of enqueued transfers. The number of transfers which can be enqueued is defined by
    // transfer_queue_size
    std::queue<IdmaTransfer *> transfer_queue;
    // Block FSM event, used to trigger all checks after something has been updated
    vp::ClockEvent fsm_event;
    // FSM state (see IDmaMe3dState), traced for profiling
    vp::Register<uint32_t> me_state;
    // Current transfer being processed, NULL when none
    IdmaTransfer *current_transfer;
    // Address of the next line to be extracted from the current transfer
    uint64_t current_src;
    uint64_t current_dst;
    // Number of lines still to be extracted from the current page
    uint64_t current_reps;
    // Number of lines per page, after normalization of the 1D case. Used to reload current_reps
    // when moving to the next page.
    uint64_t current_reps_init;
    // Number of pages still to be extracted from the current transfer, current one included
    uint64_t current_reps_3d;
};
