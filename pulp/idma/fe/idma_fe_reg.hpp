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
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <vp/register.hpp>
#include <vp/signal.hpp>
#include "../idma.hpp"

/**
 * @brief XDma front-end
 *
 * This front-end can be used to expose a register-mapped interface on a bus
 */
class IDmaFeReg : public vp::Block, public IdmaTransferProducer
{
public:
    /**
     * @brief Construct a new IDmaFeReg frontend
     *
     * @param idma The top iDMA block.
     * @param me The middle end.
     */
    IDmaFeReg(vp::Component *idma, IdmaTransferConsumer *me);

    void reset(bool active) override;

    void update() override;
    void ack_transfer(IdmaTransfer *transfer) override;

private:
    // Handle register accesses here
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    // Enqueue a transfer using the current values of the registers
    uint32_t enqueue_copy(bool &granted);

    // Pointer to middle-end
    IdmaTransferConsumer *me;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Input interface for register accesses
    vp::IoSlave input_itf;
    // Output interrupt triggered each time a transfer is finished
    vp::WireMaster<bool> irq_itf;
    // Register holding source address
    vp::Register<uint64_t> src;
    // Register holding destination address
    vp::Register<uint64_t> dst;
    vp::Register<uint64_t> length;
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
    // Signals for VCD tracing
    vp::Signal<bool> trace_busy;
    vp::Signal<uint32_t> trace_src;
    vp::Signal<uint32_t> trace_dst;
    vp::Signal<uint32_t> trace_length;
    vp::Signal<uint32_t> trace_src_stride;
    vp::Signal<uint32_t> trace_dst_stride;
    vp::Signal<uint32_t> trace_reps;
    vp::Signal<uint32_t> trace_id;
};
