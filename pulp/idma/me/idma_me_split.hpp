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
 * Authors: Marco Bertuletti, ETH Zurich (mbertuletti@iis.ee.ethz.ch)
 */

#pragma once

#include <queue>
#include <string>
#include <vp/vp.hpp>
#include "../idma.hpp"



/**
 * @brief Split middle-end
 *
 * Model of idma_split_midend: a transfer which crosses a boundary aligned on split_size bytes
 * is cut at these boundaries into several 1D transfers. The boundaries are taken on the side
 * of the transfer in the local area (loc_base / loc_size), the source if it is there, the
 * destination otherwise. A transfer which fits between two boundaries is forwarded as is.
 *
 * Placed in front of a distributed middle-end (IDmaMeDist) whose regions add up to split_size,
 * it makes sure each transfer it forwards touches every region at most once.
 *
 * Only plain copies are supported: a broadcast or a reduction (collective_op != 0) is fatal.
 */
class IDmaMeSplit : public vp::Block, public IdmaTransferConsumer, public IdmaTransferProducer
{
public:
    /**
     * @brief Construct a new split middle-end
     *
     * @param idma The top iDMA block.
     * @param cfg The configuration of the top iDMA block.
     * @param name The name of this block.
     * @param fe The previous stage, can be set later with set_fe.
     * @param be The next stage, can be set later with set_be.
     * @param split_size Size in bytes of the aligned windows transfers are cut into.
     */
    IDmaMeSplit(vp::Component *idma, const IdmaConfig &cfg, std::string name,
        IdmaTransferProducer *fe, IdmaTransferConsumer *be, uint64_t split_size);

    void reset(bool active) override;

    bool can_accept_transfer() override;
    void enqueue_transfer(IdmaTransfer *transfer) override;
    void update() override;
    void ack_transfer(IdmaTransfer *transfer) override;
    void set_fe(IdmaTransferProducer *fe) { this->fe = fe; }
    void set_be(IdmaTransferConsumer *be) { this->be = be; }

private:
    // FSM handler, called to check if any action should be taken after something was updated
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // Send the next piece of the current transfer to the next stage, if it can accept it.
    // from_enqueue is true when called while the previous stage is enqueueing the transfer, which
    // then does not need to be notified that the stage is free again.
    void send_next(bool from_enqueue);

    // Pointer to the previous stage
    IdmaTransferProducer *fe;
    // Pointer to the next stage
    IdmaTransferConsumer *be;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Size of the aligned windows, in bytes
    uint64_t split_size;
    // Local area, used to decide on which side of the transfer the boundaries are taken
    uint64_t loc_base;
    uint64_t loc_size;
    // Transfers accepted and not yet acknowledged, in order
    std::queue<IdmaTransfer *> transfer_queue;
    // Block FSM event, used to trigger all checks after something has been updated
    vp::ClockEvent fsm_event;
    // Transfer being forwarded, NULL when the stage can accept a new one
    IdmaTransfer *current_transfer;
    // True if the current transfer is forwarded as is, false if it is cut
    bool current_whole;
    // Source, destination and size of what remains to be sent of the current transfer
    uint64_t current_src;
    uint64_t current_dst;
    uint64_t current_size;
};
