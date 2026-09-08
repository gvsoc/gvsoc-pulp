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
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <vp/register.hpp>
#include <vp/signal.hpp>
#include "../idma.hpp"
#include "../idma_nd.hpp"


/*
 * Register map of the idma_reg32_3d front-end.
 *
 * The register file reserves IDMA_REG32_3D_MULTIREG_COUNT entries for each of the status, next_id
 * and done_id multiregs. The count is fixed in the generator template, not derived from the number
 * of streams the wrapper instantiates, so the descriptor block always starts at the same offset.
 *
 * These values come from the generated idma_reg32_3d_reg_pkg.sv and match the map the software is
 * compiled against, see pulp-sdk rtos/pulpos/pulp_archi/include/archi/dma/idma_v2.h. Beware that
 * pulp-runtime carries a stale copy of that header, generated from an older iDMA with sixteen
 * entries per multireg, which puts next_id at 0x44 and done_id at 0x84 instead.
 */
#define IDMA_REG32_3D_MULTIREG_COUNT 2

#define IDMA_REG32_3D_CONF 0x000
#define IDMA_REG32_3D_STATUS(stream) (0x004 + 4 * (stream))
#define IDMA_REG32_3D_NEXT_ID(stream) \
    (0x004 + 4 * (IDMA_REG32_3D_MULTIREG_COUNT + (stream)))
#define IDMA_REG32_3D_DONE_ID(stream) \
    (0x004 + 4 * (2 * IDMA_REG32_3D_MULTIREG_COUNT + (stream)))

#define IDMA_REG32_3D_DST_ADDR 0x0d0
#define IDMA_REG32_3D_SRC_ADDR 0x0d8
#define IDMA_REG32_3D_LENGTH 0x0e0
#define IDMA_REG32_3D_DST_STRIDE_2 0x0e8
#define IDMA_REG32_3D_SRC_STRIDE_2 0x0f0
#define IDMA_REG32_3D_REPS_2 0x0f8
#define IDMA_REG32_3D_DST_STRIDE_3 0x100
#define IDMA_REG32_3D_SRC_STRIDE_3 0x108
#define IDMA_REG32_3D_REPS_3 0x110

// CONF fields. enable_nd holds the number of extra dimensions: 0 for 1D, 1 for 2D, 2 for 3D.
#define IDMA_REG32_3D_CONF_ENABLE_ND_BIT 10
#define IDMA_REG32_3D_CONF_ENABLE_ND_MASK 0x3
#define IDMA_REG32_3D_CONF_SRC_PROTOCOL_BIT 12
#define IDMA_REG32_3D_CONF_DST_PROTOCOL_BIT 15
#define IDMA_REG32_3D_CONF_PROTOCOL_MASK 0x7

// Protocol encoding, as used by the software to pick the source and destination backends.
#define IDMA_REG32_3D_PROT_AXI 0
#define IDMA_REG32_3D_PROT_OBI 1
#define IDMA_REG32_3D_PROT_INIT 4


class IDmaFeReg32_3d;


/**
 * @brief One register-file port of the reg32_3d front-end
 *
 * The hardware instantiates one independent register file per requester, so that every core can
 * program a transfer without interfering with the others. Each port owns a full copy of the
 * descriptor registers and is mapped at its own address, either through the per-core demux alias
 * or through the cluster peripheral interconnect.
 *
 * The identifier counters are not part of a port: they belong to the streams and are therefore
 * held by the front-end and shared by every port.
 */
class IDmaFeReg32_3dPort : public vp::Block
{
    friend class IDmaFeReg32_3d;

public:
    /**
     * @brief Construct a new register-file port
     *
     * @param fe The front-end owning this port.
     * @param idma The top iDMA block, used to declare the slave interface.
     * @param name Name of the port, also used to name the slave interface.
     * @param id Index of the port.
     */
    IDmaFeReg32_3dPort(IDmaFeReg32_3d *fe, vp::Component *idma, std::string name, int id);

private:
    // Handle register accesses on this port
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    // Front-end owning this port
    IDmaFeReg32_3d *fe;
    // Index of this port, only used for traces
    int id;
    // Input interface for register accesses
    vp::IoSlave input_itf;
    // Descriptor registers, one full set per port
    vp::Register<uint32_t> conf;
    vp::Register<uint32_t> src;
    vp::Register<uint32_t> dst;
    vp::Register<uint32_t> length;
    vp::Register<uint32_t> src_stride_2;
    vp::Register<uint32_t> dst_stride_2;
    vp::Register<uint32_t> reps_2;
    vp::Register<uint32_t> src_stride_3;
    vp::Register<uint32_t> dst_stride_3;
    vp::Register<uint32_t> reps_3;
};


/**
 * @brief idma_reg32_3d front-end
 *
 * Register-mapped front-end matching the idma_reg32_3d register file that pulp_cluster
 * instantiates in its iDMA-based dmac_wrap. It exposes one independent register-file port per
 * requester and a set of transfer identifier counters per stream.
 *
 * A transfer is launched by reading NEXT_ID of a stream: the descriptor currently programmed on
 * that port is pushed to the middle-end and the read returns the identifier allocated for it, or
 * zero if the transfer could not be accepted. Software waits for a transfer by polling DONE_ID of
 * the same stream, which returns the identifier of the last completed transfer.
 *
 * Completion is also signalled through the event ports. Following the hardware, the event is
 * broadcast to every core rather than routed to the one that programmed the transfer, and no
 * interrupt is generated.
 */
class IDmaFeReg32_3d : public vp::Block, public IdmaTransferProducer
{
    friend class IDmaFeReg32_3dPort;

public:
    /**
     * @brief Construct a new IDmaFeReg32_3d front-end
     *
     * @param idma The top iDMA block.
     * @param me The middle end.
     */
    IDmaFeReg32_3d(vp::Component *idma, IdmaTransferConsumer *me);

    void reset(bool active) override;

    void update() override;
    void ack_transfer(IdmaTransfer *transfer) override;

private:
    // Per-stream transfer identifier counters and busy accounting
    struct Stream
    {
        // Identifier which will be allocated to the next transfer launched on this stream
        vp::Register<uint32_t> next_id;
        // Identifier of the last transfer completed on this stream
        vp::Register<uint32_t> done_id;
        // Number of transfers launched on this stream and not completed yet
        int pending;

        Stream(vp::Block &parent, int id);
    };

    // Build a transfer from the registers of a port and push it to the middle-end. Returns the
    // allocated identifier, or 0 if the transfer was rejected.
    uint32_t enqueue_copy(IDmaFeReg32_3dPort *port, int stream);
    // Push as many pending transfers as the middle-end accepts
    void check_pending_queue();
    // Broadcast a completion event to every core and PE port
    void raise_event();

    // Pointer to middle-end
    IdmaTransferConsumer *me;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Number of register-file ports, one per requester
    int nb_ports;
    // Number of streams, each with its own identifier counters
    int nb_streams;
    // Number of cores receiving the completion event
    int nb_cores;
    // Maximum number of transfers waiting to be handed over to the middle-end
    int global_queue_depth;
    // Register-file ports
    std::vector<IDmaFeReg32_3dPort *> ports;
    // Per-stream identifier counters
    std::vector<Stream *> streams;
    // Completion event, broadcast to every core
    std::vector<vp::WireMaster<bool> *> event_itf;
    // Completion event, broadcast to every PE port
    std::vector<vp::WireMaster<bool> *> event_pe_itf;
    // Transfers accepted but not handed over to the middle-end yet
    std::queue<IdmaTransfer *> pending_queue;
    // Signals for VCD tracing
    vp::Signal<bool> trace_busy;
    vp::Signal<uint32_t> trace_src;
    vp::Signal<uint32_t> trace_dst;
    vp::Signal<uint32_t> trace_length;
    vp::Signal<uint32_t> trace_id;
};
