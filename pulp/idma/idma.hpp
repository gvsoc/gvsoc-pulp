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

#include <vector>
#include <vp/vp.hpp>



/**
 * @brief iDMA transfer
 *
 * This class is used to exchange transfer information betweem frontends, middle-ends and backends.
 */
class IdmaTransfer
{
public:
    // Transfer source address
    uint64_t src;
    // Transfer destination address
    uint64_t dst;
    // Transfer size
    uint64_t size;
    // Transfer source stride
    uint64_t src_stride;
    // Transfer destination stride
    uint64_t dst_stride;
    // Transfer repetitions
    uint64_t reps;
    // Transfer config
    uint64_t config;

    // Free rom for additional information
    std::vector<uint64_t> data;

    // Number of bursts of the transfer. This may be used for splitting a transfer into bursts
    int nb_bursts;
    // Tell if all bursts have been sent
    bool bursts_sent;
    // Parent transfer. In case the parent has been split into several simpler transfers,
    // this field is set in the bursts to the parent transfer
    IdmaTransfer *parent;
};



class IdmaTransferProducer;

/**
 * @brief Transfer consumer interface
 *
 * This interface is implemented by middle-ends and backends to receive transfers to be handled.
 */
class IdmaTransferConsumer
{
public:
    /**
     * @brief Enqueue a transfer
     *
     * This pass a transfer to be executed to the next stage in the iDMA.
     *
     * @param transfer Pointer to the transfer
     */
    virtual void enqueue_transfer(IdmaTransfer *transfer) = 0;

    /**
     * @brief Ask if the next stage is ready to accept a transfer
     *
     * This must be called before called enqueue_transfer to know if the transfer can be handled
     *
     * @return True if it can accept a transfer
     */
    virtual bool can_accept_transfer() = 0;
};



/**
 * @brief Transfer producer interface
 *
 * This interface is implemented by frontends and middle-ends to received synchronization about
 * transfers pushed to be processed.
 */
class IdmaTransferProducer
{
public:
    /**
     * @brief Update notification
     *
     * This notifies the previous stage in the iDMA that something has changed and that his FSM
     * should be scheduled to check if any action should be taken.
     */
    virtual void update() = 0;

    /**
     * @brief Acknowledge a transfer
     *
     * This must be called to notify that a transfer is done
     *
     * @param transfer Pointer to the transfer
     */
    virtual void ack_transfer(IdmaTransfer *transfer) = 0;
};
