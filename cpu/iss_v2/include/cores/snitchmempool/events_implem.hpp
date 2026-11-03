// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/cores/snitchmempool/events.hpp>
#include <cpu/iss_v2/include/event/event_implem.hpp>

inline void SnitchMempoolEvents::event_insn_latency_account(iss_insn_t *insn, int latency)
{
    int64_t done = this->iss.clock.get_cycles() + latency;
    if (done <= this->last_done)
    {
        done = this->last_done + 1;
    }
    this->last_done = done;

    // The exec path released the destination when the handler wrote it;
    // hold it again until the result is written back
    uint64_t mask = insn->sb_out_reg_mask;
    if (mask != 0)
    {
        for (uint64_t m = mask; m != 0; m &= m - 1)
        {
            this->iss.regfile.sb_reg_invalid_set(__builtin_ctzll(m));
        }
        this->pending.push_back({mask, done});
        this->schedule_release();
    }
}
