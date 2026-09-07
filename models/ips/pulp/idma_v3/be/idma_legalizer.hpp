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
#include "../idma.hpp"
#include "idma_manager.hpp"

class IdmaBackend;

/**
 * @brief The legalizer of the back-end (idma_legalizer_*).
 *
 * Holds one 1D request at a time in two side registers (read and write) and
 * cuts each side into bursts the protocol accepts, at most one read split and
 * one write split per cycle:
 *
 * - each side may not cross the boundary its manager reports
 *   (bytes_to_page_boundary(): the AXI page of 2^(offset + burst_len) bytes,
 *   or one bus word for the non-bursting protocols);
 * - in coupled mode (both protocols burst and decouple_rw is clear) both
 *   sides take the shorter of the two and advance together;
 * - otherwise (decoupled) each side advances on its own;
 * - a side only advances when its FIFO can take the split and its manager's
 *   address channel is ready;
 * - the next 1D request is taken in the cycle of the last split, so
 *   consecutive 1D transfers leave no bubble; the first split of a request
 *   comes one cycle after it was taken;
 * - a zero-length request is answered at once without touching the sides.
 */
class IdmaLegalizer
{
public:
    /// @param be    Owning back-end (its FIFOs, managers and mid-end are used).
    /// @param width Data path width in bytes.
    IdmaLegalizer(IdmaBackend *be, int width);

    /// Drop the current request and both sides (hardware reset).
    void reset();

    /// One evaluation for cycle @p now. Returns true when something happened
    /// (a split was emitted or a request was taken), i.e. the pipeline should
    /// be evaluated again next cycle.
    bool step(int64_t now);

    /// True while the read side still has bytes to split (r_leg_busy).
    bool r_busy() const { return this->r.valid; }
    /// True while the write side still has bytes to split (w_leg_busy).
    bool w_busy() const { return this->w.valid; }

private:
    /// One side of the current request (the RTL r_tf_q / w_tf_q register).
    struct Side
    {
        /// Bytes remain to be split on this side.
        bool valid = false;
        /// Address of the next split.
        uint64_t addr = 0;
        /// Bytes left to split.
        uint64_t length = 0;
        /// Byte rotation applied to this side's beats (read or write shift).
        uint8_t shift = 0;
    };

    /// Try to take the next 1D request from the mid-end.
    bool take_request(int64_t now);
    /// Build the split of one side.
    IdmaSplit make_split(const Side &side, uint32_t num_bytes);

    IdmaBackend *be;
    /// Data path width in bytes and its log2.
    int width;
    int log2_width;

    /// Read and write sides of the request being split.
    Side r;
    Side w;
    /// The 1D request being split (NULL when idle).
    Idma1dReq *req = nullptr;
    /// Managers serving the request's source and destination protocols.
    IdmaReadManager *rm = nullptr;
    IdmaWriteManager *wm = nullptr;
    /// Both sides advance in lock-step (see the class description).
    bool coupled = false;
    /// Cycle in which a request was last taken (one per cycle).
    int64_t last_take_cycle = -1;
};
