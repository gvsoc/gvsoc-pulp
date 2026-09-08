// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/cores/spatz/events.hpp>
#include <cpu/iss_v2/include/event/event_implem.hpp>

inline void SpatzEvents::event_div_account(iss_reg_t dividend, iss_reg_t divisor,
                                           bool is_signed, bool is_rem)
{
    // The 32-bit lane divides the operands; the 64-bit lane divides their
    // sign halves (each 32-bit operand is sign-extended to 64 bits by
    // Snitch, then the upper element is widened again per the operation's
    // signedness by the IPU). The result waits for the slower lane.
    uint32_t a = (uint32_t)dividend;
    uint32_t b = (uint32_t)divisor;
    uint64_t a_hi = (int32_t)a < 0 ? (is_signed ? ~(uint64_t)0 : 0xffffffffULL) : 0;
    uint64_t b_hi = (int32_t)b < 0 ? (is_signed ? ~(uint64_t)0 : 0xffffffffULL) : 0;

    int it_lo = serdiv_iterations(32, is_signed, a, b);
    int it_hi = serdiv_iterations(64, is_signed, a_hi, b_hi);
    int it = it_lo > it_hi ? it_lo : it_hi;

    // Zero iterations still cost the load and finish cycles; an immediate
    // termination (the quotient is known to be zero) saves one of them.
    this->div_latency = DIV_BASE_LATENCY + (it < 0 ? -1 : it);
}

inline void SpatzEvents::event_insn_latency_account(iss_insn_t *insn, int latency)
{
    int64_t now = this->iss.clock.get_cycles();

    // The decoder tags multiplies with their latency and divides with a
    // marker; a divide's cost was computed by event_div_account from its
    // operands, just before this hook.
    int cost = latency == DIV_TAG_LATENCY ? this->div_latency : latency;

    // Responses come back in order, one per cycle at most
    int64_t done = now + cost;
    if (done <= this->last_done)
    {
        done = this->last_done + 1;
    }
    this->last_done = done;

    // The exec path released the destination when the handler wrote it;
    // hold it again until the result comes back: a dependent (or a second
    // writer) issues on the writeback cycle.
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
