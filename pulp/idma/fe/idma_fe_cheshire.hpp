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
 *          Lorenzo Ruotolo, Politecnico di Torino (lorenzo.ruotolo@polito.it)
 */

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss/include/offload.hpp>
#include <vp/register.hpp>
#include <vp/signal.hpp>
#include <vp/itf/io.hpp>
#include "../idma.hpp"
#include "idma_fe_cheshire_regs.hpp"

/**
 * @brief Cheshire DMA front-end
 *
 * This front-end can be used to connect a core to the iDMA and manage transfers through its registers.
 */
class IDmaFeCheshire : public vp::Block, public IdmaTransferProducer
{
public:
    /**
     * @brief Construct a new IDmaFeCheshire frontend
     *
     * @param idma The top iDMA block.
     * @param me The middle end.
     */
    IDmaFeCheshire(vp::Component *idma, IdmaTransferConsumer *me);

    void reset(bool active) override;

    void update() override;
    void ack_transfer(IdmaTransfer *transfer) override;

private:
    // Method for handling requests from the core
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    // Enqueue a transfer using the current values of the registers
    uint64_t enqueue_copy(void);

    // Bool for transfer grant from the middle-end
    bool transfer_granted;
    // Pointer to middle-end
    IdmaTransferConsumer *me;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Interface from which the requests are received from the core
    vp::IoSlave input_itf;
    // Interface for granting previously stalled cheshire dma offload
    vp::WireMaster<IssOffloadInsnGrant<uint64_t> *> offload_grant_itf;

    // Register holding source address
    vp::Register<uint64_t> src;
    // Register holding destination address
    vp::Register<uint64_t> dst;
    // Register holding number of bytes to transfer
    vp::Register<uint64_t> num_bytes;
    // Register holding transfer config
    vp::Register<uint64_t> config;
    // Register holding source stride
    vp::Register<uint64_t> src_stride;
    // Register holding destination stride
    vp::Register<uint64_t> dst_stride;
    // Register holding replication
    vp::Register<uint64_t> reps;
    // Transfer ID of the next transfer
    vp::Register<uint64_t> next_transfer_id;
    // Transfer ID of the last completed ID
    vp::Register<uint64_t> completed_id;

    // When a transfer is blocked, once the middle-end is ready to accept transfer,
    // send a grant to the core to unblock it
    vp::Signal<bool> do_transfer_grant;
    // In case a transfer was blocked, gives the transfer which was blocked
    IdmaTransfer *stalled_transfer;
};
