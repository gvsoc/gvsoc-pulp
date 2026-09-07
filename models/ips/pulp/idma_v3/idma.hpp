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

#include <cstdint>

/**
 * @file idma.hpp
 * @brief Shared descriptors and inter-block interfaces of the iDMA v3 model.
 *
 * The model follows the block structure of the pulp-platform iDMA RTL:
 *
 *   front-end -> request FIFO -> ND mid-end -> back-end
 *   back-end  = legalizer -> {r_dp_req, w_dp_req, w_last} FIFOs
 *             -> read manager -> byte-lane buffer -> write manager
 *
 * Every block is a vp::Block child of the top vp::Component; the interfaces
 * below are the "wires" between them. The names mirror the RTL where a
 * direct counterpart exists (idma_req_t, r_dp_req_t / w_dp_req_t, ...).
 *
 * Life of a transfer
 * ------------------
 *
 * 1. The front-end decodes a launch (a NEXT_ID register read, or a dmcpy
 *    instruction) into an IdmaNdReq and pushes it into the mid-end's request
 *    FIFO (IdmaFeSink::push_nd). A full FIFO denies the launch; the mid-end
 *    calls IdmaFeSource::me_ready() once a slot is usable and the front-end
 *    retries it.
 * 2. The back-end's legalizer, when idle, pulls the next 1D request out of
 *    the mid-end (IdmaMeSource::peek_1d / take_1d). The mid-end builds one
 *    Idma1dReq per line of the ND transfer, flagging the last one super_last.
 * 3. The legalizer cuts the 1D request into read splits and write splits
 *    (IdmaSplit), each one burst legal for its protocol, and hands them to
 *    the read manager (issue_ar) and the write manager (issue_aw), keeping a
 *    copy in the r_fifo / w_fifo of the back-end so the data beats can be
 *    matched to their burst later.
 * 4. Response beats of a read burst arrive at the read manager, which asks
 *    the back-end to push them into the byte-lane buffer; one cycle later
 *    the write manager finds the bytes of its next beat visible, pops them
 *    and sends the data beat. Alignment is handled by the lane indexing of
 *    the buffer (see IdmaByteLaneBuffer).
 * 5. The response of a write burst (AXI B, OBI rvalid) pops the wlast FIFO;
 *    the entry flagged last completes the 1D request (IdmaMeSource::
 *    complete_1d), and the 1D flagged super_last completes the ND transfer
 *    towards the front-end (IdmaFeSource::complete_nd), which updates
 *    DONE_ID and pulses the event one cycle later.
 *
 * Cycle model
 * -----------
 *
 * There is no per-cycle polling. Each back-end owns one clock event, the
 * tick, evaluated only in cycles where something can progress; every stall
 * is released by the callback that ends it (a response beat, a bus retry, a
 * FIFO pop, a launch), which calls IdmaBeWake::wake(). Timing comes from the
 * structures rather than from constants: the FIFOs are registered (pushed at
 * N, visible at N + 1), so is the buffer, and all of them take the current
 * cycle as argument so the result does not depend on the order in which the
 * callbacks of one cycle run.
 */

/// Protocol encodings as software writes them into CONF (idma_pkg::protocol_e).
enum IdmaProtocol
{
    IDMA_PROT_AXI  = 0,
    IDMA_PROT_OBI  = 1,
    IDMA_PROT_INIT = 4,
};

/// One launched ND transfer as produced by a front-end (the nd_req_t of the
/// RTL midend, plus the bookkeeping the model needs to free it).
struct IdmaNdReq
{
    uint64_t src = 0;
    uint64_t dst = 0;
    uint64_t length = 0;
    /// Strides and repetitions of dimensions 2 and 3 (index 0 = dim 2).
    uint64_t src_stride[2] = {0, 0};
    uint64_t dst_stride[2] = {0, 0};
    uint64_t reps[2] = {1, 1};
    /// CONF.enable_nd: 0 = 1D, 1 = 2D, 2 = 3D.
    int nd = 0;
    int src_prot = IDMA_PROT_AXI;
    int dst_prot = IDMA_PROT_AXI;
    /// idma_pkg::options_t bits the legalizer honours.
    bool decouple_rw = false;
    bool decouple_aw = false;
    /// Transfer id returned to software and stream the request was launched on.
    uint32_t id = 0;
    int stream = 0;
    /// Number of 1D children still alive (memory bookkeeping only).
    int nb_1d_alive = 0;
};

/// One 1D request as handed by the mid-end to the back-end (idma_req_t).
struct Idma1dReq
{
    uint64_t src = 0;
    uint64_t dst = 0;
    uint64_t length = 0;
    int src_prot = IDMA_PROT_AXI;
    int dst_prot = IDMA_PROT_AXI;
    bool decouple_rw = false;
    bool decouple_aw = false;
    /// opt.last: this is the last 1D of its ND transfer.
    bool super_last = true;
    IdmaNdReq *parent = nullptr;
};

/// One legal burst on one side, as emitted by the legalizer (r_dp_req_t or
/// w_dp_req_t together with its AR / AW meta channel).
///
/// The beats of a split are bus words: beat k covers the word-aligned
/// addresses [base + k * width, base + (k + 1) * width) where base is addr
/// rounded down to the width. Only the bytes of the split are valid in the
/// first beat (from offset) and in the last one (up to tailer), which is
/// what IdmaBackend::beat_mask() computes.
struct IdmaSplit
{
    /// Unaligned first byte address of the split.
    uint64_t addr = 0;
    /// Bytes of the split.
    uint32_t num_bytes = 0;
    /// Leading invalid bytes of the first beat (addr & (w - 1)).
    uint8_t offset = 0;
    /// (num_bytes + offset) & (w - 1); 0 means the last beat is full.
    uint8_t tailer = 0;
    /// read_shift or write_shift of the transfer (byte rotation).
    uint8_t shift = 0;
    /// Beats of the split (AXI: AxLEN + 1).
    uint16_t num_beats = 1;
    /// Single-beat split.
    bool is_single = true;
    /// Write side: last split of the 1D / last split of the ND transfer.
    bool last = false;
    bool super_last = false;
    bool decouple_aw = false;
    Idma1dReq *req = nullptr;
};

/// Back-end parameters (the idma_backend RTL parameters that shape timing).
struct IdmaBackendParams
{
    /// Data path width in bytes (StrbWidth).
    int width = 8;
    /// Depth of r_dp_req / w_dp_req (outstanding AR / AW).
    int num_ax_in_flight = 8;
    /// Depth of every byte lane of the buffer.
    int buffer_depth = 3;
    /// Depth of the w_last FIFO (writes awaiting their B).
    int meta_fifo_depth = 11;
};

/**
 * Mid-end as seen by a front-end: the request FIFO in front of the ND
 * splitter.
 */
class IdmaFeSink
{
public:
    /// True when the request FIFO can take one more ND transfer.
    virtual bool can_accept_nd() = 0;
    /// Push an ND transfer; it becomes visible to the splitter after one cycle
    /// plus extra_delay (the datapath clock-gate wake-up).
    virtual void push_nd(IdmaNdReq *req, int extra_delay) = 0;
    /// True while an ND transfer is queued or being split.
    virtual bool is_busy() = 0;
};

/**
 * Front-end (one stream of it) as seen by the mid-end.
 */
class IdmaFeSource
{
public:
    /// A request FIFO slot was freed: a denied launch may be retried now.
    virtual void me_ready() = 0;
    /// The last 1D of this ND transfer completed (its last write acknowledged).
    /// The request is still valid during the call and freed afterwards.
    virtual void complete_nd(IdmaNdReq *req) = 0;
};

/**
 * Mid-end as seen by the back-end (pull model: the legalizer asks for the next
 * 1D request when it is idle).
 */
class IdmaMeSource
{
public:
    /// Next 1D request visible this cycle, or NULL. Ownership stays with the
    /// mid-end; the back-end gives it back through complete_1d().
    virtual Idma1dReq *peek_1d(int64_t now) = 0;
    /// Consume the request returned by peek_1d() (the RTL handshake).
    virtual void take_1d(int64_t now) = 0;
    /// The last write of this 1D was acknowledged (or it was rejected as
    /// zero-length, in which case the RTL flags it as the last of its ND
    /// transfer whatever its position).
    virtual void complete_1d(Idma1dReq *req, bool force_last) = 0;
};

/**
 * Back-end as seen by the blocks feeding it.
 */
class IdmaBeWake
{
public:
    /// Something the back-end waits for may have changed: evaluate the
    /// pipeline again in @p cycles cycles (the earliest request wins).
    virtual void wake(int cycles=1) = 0;
};
