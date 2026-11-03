// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <deque>
#include <vp/vp.hpp>
#include <cpu/iss_v2/include/event/event.hpp>

/**
 * @brief Timing events of the MemPool Snitch: pipelined FPU operations.
 *
 * An instruction whose decoder item has a latency (set from the ISA
 * generator, e.g. vfexp.h) does not stall the core: its
 * destination registers are held in the scoreboard until the result is
 * written back, `latency` cycles after issue, so only a dependent (or a
 * second writer) waits. Results come back in order, one per cycle at most.
 */
class SnitchMempoolEvents : public Events
{
public:
    SnitchMempoolEvents(Iss &iss);

    void reset(bool active);

    inline void event_insn_latency_account(iss_insn_t *insn, int latency);

private:
    /// Writeback of a pending result: release its registers.
    static void release_handler(vp::Block *__this, vp::ClockEvent *event);
    void schedule_release();

    struct PendingResult
    {
        uint64_t reg_mask;
        int64_t done;
    };
    /// Results in flight, in issue order.
    std::deque<PendingResult> pending;
    vp::ClockEvent release_event;
    /// Writeback cycle of the last pending result (in-order writeback).
    int64_t last_done = -1;
};
