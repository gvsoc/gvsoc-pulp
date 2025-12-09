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

#include <string>
#include <iostream>
#include <sstream>
#include <vp/vp.hpp>
#include <cpu/iss/include/offload.hpp>
#include <vp/register.hpp>
#include <vp/signal.hpp>
#include "../idma.hpp"

/**
 * @brief XDma front-end
 *
 * This front-end can be used to connect a core to the iDMA through the custom XDMa instructions
 */
class IDmaFeXdma : public vp::Block, public IdmaTransferProducer
{
public:
    /**
     * @brief Construct a new IDmaFeXdma frontend
     *
     * @param idma The top iDMA block.
     * @param me The middle end.
     */
    IDmaFeXdma(vp::Component *idma, IdmaTransferConsumer *me);

    void reset(bool active) override;

    void update() override;
    void ack_transfer(IdmaTransfer *transfer) override;

private:
    // Method for offload interface, called when the core is offloading an xdma instruction
    static void offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    // Enqueue a transfer using the current values of the registers
    uint32_t enqueue_copy(uint32_t config, uint32_t size, bool &granted, uint32_t collective_type);
    // Return status
    uint32_t get_status(uint32_t status);

    // Pointer to middle-end
    IdmaTransferConsumer *me;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Interface from which the instructions are received from the core
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf;
    // Interface for granting previously stalled xdma offload
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf;
    // Register holding source address
    vp::Register<uint64_t> src;
    // Register holding destination address
    vp::Register<uint64_t> dst;
    // Register holding source stride
    vp::Register<uint64_t> src_stride;
    // Register holding destination stride
    vp::Register<uint64_t> dst_stride;
    // Register holding replication
    vp::Register<uint32_t> reps;
    // Transfer ID of the next transfer
    vp::Register<uint32_t> next_transfer_id;
    // Transfer ID of the last completed ID
    vp::Register<uint32_t> completed_id;
    // When a transfer is blocked, once the middle-end is ready to accept transfer,
    // send a grant to the core to unblock it
    vp::Signal<bool> do_transfer_grant;
    // In case a transfer was blocked, gives the transfer which was blocked
    IdmaTransfer *stalled_transfer;
#ifdef ENABLE_DMA_SIMPLE_COLLECTIVE_IMPLEMENTATION
    // Transfer collective
    uint16_t collective_row_mask;
    uint16_t collective_col_mask;
#endif //ENABLE_DMA_SIMPLE_COLLECTIVE_IMPLEMENTATION

    //track iDMA transfer time
    int64_t transfer_start_time;
    int64_t num_inflight_transfer;
    int64_t total_idma_used_time;
    std::string TxnList;
};
