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

#include <map>
#include <string>
#include <vector>
#include <vp/vp.hpp>
#include <vp/signal.hpp>
#include "../idma.hpp"
#include "idma_manager.hpp"
#include "idma_fifo.hpp"
#include "idma_buffer.hpp"
#include "idma_legalizer.hpp"

/**
 * @brief One back-end (idma_backend): legalizer, ordering FIFOs, byte-lane
 * buffer, and the services the protocol managers use.
 *
 * Every stream of a DMA has one back-end pulling 1D requests from its mid-end.
 * Protocol managers are registered per protocol on the read and on the write
 * side; a request whose source or destination protocol has no manager is
 * fatal (the RTL would hang).
 *
 * Timing structure (all per stream):
 *
 * - r_fifo / w_fifo: the r_dp_req / w_dp_req FIFOs of depth num_ax_in_flight
 *   (outstanding read and write bursts), registered;
 * - wlast: the w_last FIFO of depth meta_fifo_depth, one entry per write
 *   burst until its response; the response of a burst flagged last completes
 *   the 1D request;
 * - buffer: the byte-lane buffer, pushed by response beats, popped by data
 *   beats one cycle later at the earliest.
 *
 * The per-cycle evaluation runs in one ClockEvent, in the order legalizer,
 * write managers (pops), read managers' held beats (pushes), and only re-arms
 * itself when something can progress without an external event; external
 * events (launch, response beats, retries) call wake().
 *
 * Why this order: in the RTL the buffer lanes are passthrough FIFOs, a full
 * lane accepts a push in the cycle it is popped. Response beats reach the
 * model through callbacks that may run before or after the tick of the same
 * cycle; a beat arriving before the tick while the buffer is full is denied
 * (held by its producer), the tick pops the write beat, then re-offers the
 * held beat in the same cycle. A beat arriving after the tick simply sees
 * the room. Either way the beat enters in the same cycle as it would in the
 * RTL.
 *
 * Matching beats to bursts: response beats are matched to the split heading
 * r_fifo (the RTL has a single r_dp_req FIFO, so beats come back in split
 * order whatever the protocol), data beats to the split heading w_fifo; a
 * beat counter per FIFO head tracks progress inside a burst.
 */
class IdmaBackend : public vp::Block, public IdmaBeWake
{
    friend class IdmaLegalizer;

public:
    /// @param top    Owning component (the block is created as its child).
    /// @param name   Block name (be, be0, be1), prefix of the traces.
    /// @param params FIFO depths, buffer depth and data path width.
    /// @param me     Mid-end the 1D requests are pulled from.
    IdmaBackend(vp::Component *top, std::string name, const IdmaBackendParams &params,
        IdmaMeSource *me);

    /// Hardware reset: empties the legalizer, the FIFOs and the buffer.
    void reset(bool active) override;

    /// Register the manager serving one protocol on the read side; a request
    /// whose source protocol has no manager is fatal.
    void add_read_manager(IdmaReadManager *manager);
    /// Register the manager serving one protocol on the write side.
    void add_write_manager(IdmaWriteManager *manager);

    /// Read managers: true when the response beat of the burst heading the
    /// read FIFO fits into the buffer this cycle. @p token is the one
    /// issue_ar() returned for the burst; a beat of another burst of the
    /// same manager is a fatal ordering error, a beat of another manager's
    /// burst simply waits its turn.
    bool read_beat_can_accept(IdmaReadManager *manager, void *token, int64_t now);
    /// Read managers: push one response beat (a full-width aligned bus word;
    /// only the lanes of the current beat are taken).
    void read_beat_accept(IdmaReadManager *manager, void *token, const uint8_t *bus, int64_t now);

    /// Write managers: true when the next data beat of the burst heading the
    /// write FIFO can be assembled this cycle; returns its split, beat index
    /// and bus-space byte mask.
    bool write_beat_ready(IdmaWriteManager *manager, IdmaSplit **split, int *beat_idx,
        uint64_t *mask_out, int64_t now);
    /// Write managers: pop the bytes of that beat into a full-width bus word.
    void write_beat_take(IdmaWriteManager *manager, uint8_t *bus, int64_t now);
    /// Write managers: the response of the oldest outstanding write burst
    /// arrived (B / rvalid); completes the 1D request when that burst was
    /// its last one. @p error reports a failed burst (traced only).
    void write_burst_done(IdmaWriteManager *manager, bool error);

    /// Byte mask (bus space) of beat @p beat_idx of a split.
    uint64_t beat_mask(const IdmaSplit &split, int beat_idx) const;

    /// Evaluate the pipeline again in @p cycles cycles (IdmaBeWake).
    void wake(int cycles=1) override;

    /// idma_pkg::idma_busy_t as reported in STATUS bits 7:0.
    uint32_t busy_bits();
    /// True while any part of the back-end holds work.
    bool is_busy();

    /// Current cycle of the block's clock domain.
    int64_t cycles() { return this->clock.get_cycles(); }

    /// Data path width in bytes.
    int width() const { return this->params.width; }

private:
    /// Entry of the read FIFO (r_dp_req): an issued read burst.
    struct ReadSlot
    {
        IdmaSplit split;
        /// Manager the burst was issued to.
        IdmaReadManager *manager;
        /// Token the manager returned for it, echoed by its response beats.
        void *token;
    };
    /// Entry of the write FIFO (w_dp_req): an issued write burst.
    struct WriteSlot
    {
        IdmaSplit split;
        IdmaWriteManager *manager;
    };
    /// Entry of the w_last FIFO: a write burst awaiting its response.
    struct LastSlot
    {
        /// 1D request the burst belongs to.
        Idma1dReq *req;
        /// Last burst of that request: its response completes it.
        bool last;
    };

    /// The per-cycle evaluation (see the class description).
    static void tick_handler(vp::Block *__this, vp::ClockEvent *event);

    /// Manager registered for a protocol, NULL when none.
    IdmaReadManager *read_manager(int protocol);
    IdmaWriteManager *write_manager(int protocol);

    IdmaBackendParams params;
    /// Mid-end the 1D requests come from.
    IdmaMeSource *me;
    vp::Trace trace;
    /// The single per-cycle event of the back-end.
    vp::ClockEvent tick;
    /// Earliest cycle a wake() asked for; the engine keeps only the earliest
    /// pending enqueue, so a later request made while the tick is pending
    /// must be re-armed by the tick itself.
    int64_t wake_target = -1;

    IdmaLegalizer legalizer;
    /// Outstanding read bursts (r_dp_req), depth num_ax_in_flight.
    IdmaTimedFifo<ReadSlot> r_fifo;
    /// Outstanding write bursts whose data is not all sent (w_dp_req).
    IdmaTimedFifo<WriteSlot> w_fifo;
    /// Write bursts awaiting their response (w_last), depth meta_fifo_depth.
    IdmaTimedFifo<LastSlot> wlast;
    /// The data path between the read and the write managers.
    IdmaByteLaneBuffer buffer;
    /// Beats already consumed of the bursts heading r_fifo / w_fifo.
    int r_beat_idx = 0;
    int w_beat_idx = 0;

    /// Managers by protocol, and in registration order for the tick.
    std::map<int, IdmaReadManager *> read_managers;
    std::map<int, IdmaWriteManager *> write_managers;
    std::vector<IdmaReadManager *> read_manager_list;
    std::vector<IdmaWriteManager *> write_manager_list;

    /// GUI signals: bytes held in the buffer, back-end busy.
    vp::Signal<int> buffer_fill;
    vp::Signal<bool> busy;
};
