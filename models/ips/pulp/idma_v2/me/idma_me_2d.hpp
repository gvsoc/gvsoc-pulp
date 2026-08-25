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

#include <vp/vp.hpp>
#include "../idma.hpp"



/**
 * @brief 2D middle-end
 *
 * This front-end can be used to get support for 2D transfers
 */
class IDmaMe2D : public vp::Block, public IdmaTransferConsumer, public IdmaTransferProducer
{
public:
    /**
     * @brief Construct a new IDmaFeXdma frontend
     *
     * @param idma The top iDMA block.
     * @param fe The front end.
     * @param be The back end.
     */
    IDmaMe2D(vp::Component *idma, IdmaTransferProducer *fe, IdmaTransferConsumer *be);

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
    // Current transfer being processed
    IdmaTransfer *current_transfer;
    // Current source address of the current transfer, updated each time a burst is sent
    uint64_t current_src;
    // Current destination address of the current transfer, updated each time a burst is sent
    uint64_t current_dst;
    // Current replication of the current transfer, updated each time a burst is sent
    uint64_t current_reps;
};
