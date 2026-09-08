// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <deque>
#include <vp/vp.hpp>
#include <cpu/iss_v2/include/event/event.hpp>

/**
 * @brief Snitch + Spatz timing events: the scalar M-extension offload.
 *
 * Snitch has no integer multiplier or divider. mul / mulh* / div* / rem*
 * are offloaded to Spatz (spatz_decoder.sv, "Scalar multiplication" /
 * "Scalar division"), executed by the VFU's integer unit and written back
 * through the accelerator response port, in order. Snitch itself does not
 * stall: it keeps issuing, and only an instruction that reads (or writes)
 * the pending destination waits on its scoreboard. The RTL numbers, from
 * tests/calibration/targets/spatz/muldiv on the Verilator model:
 *
 *   - mul / mulh*: the result retires 5 cycles after issue, back-to-back
 *     multiplies pipeline one per cycle.
 *   - div / rem: 7 cycles plus the iterations of the serial divider
 *     (spatz_serdiv.sv). Snitch sign-extends both operands to 64 bits
 *     (snitch.sv, data_arga) and the decoder issues the operation with
 *     SEW=32, so the 64-bit IPU (spatz_ipu.sv, gen_64b_ipu) runs two
 *     32-bit elements: the 32-bit lane gets the operands, the 64-bit lane
 *     gets their sign halves, and the result waits for both. The divider
 *     iterates lzc(divisor) - lzc(dividend) times, or the full width when
 *     the divisor is zero, which is what the sign-half lane sees for any
 *     non-negative divisor: 64 iterations, 71 cycles to the result, for
 *     almost every divide. Only all-ones sign halves (unsigned forms with
 *     both operands >= 2^31) let that lane finish early.
 *   - Results return in order: an operation issued behind a divide
 *     completes the cycle after it.
 *
 * The retiring instruction's destination registers are re-flagged in the
 * scoreboard and released by a clock event when the result would have
 * been written back.
 */
class SpatzEvents : public Events
{
public:
    SpatzEvents(Iss &iss);

    void reset(bool active);

    inline void event_insn_latency_account(iss_insn_t *insn, int latency);
    inline void event_div_account(iss_reg_t dividend, iss_reg_t divisor,
                                  bool is_signed, bool is_rem);

    /// Cycles from a multiply's issue to its result.
    static constexpr int MUL_LATENCY = 5;
    /// Cycles from a divide's issue to its result, on top of the divider
    /// iterations.
    static constexpr int DIV_BASE_LATENCY = 7;
    /// Latency value the decoder tags div / rem items with (a marker: the
    /// real cost comes from event_div_account with the operands).
    static constexpr int DIV_TAG_LATENCY = 1;

private:
    /// Iterations of the serial divider of one lane of @p width bits, or
    /// -1 when it terminates at once (|divisor| > |dividend|).
    static int serdiv_iterations(int width, bool is_signed, uint64_t a, uint64_t b);
    /// Deferred writeback of an offloaded result: release its registers.
    static void release_handler(vp::Block *__this, vp::ClockEvent *event);
    void schedule_release();

    struct PendingResult
    {
        uint64_t reg_mask;
        int64_t done;
    };
    /// Offloaded results in flight, in issue order.
    std::deque<PendingResult> pending;
    vp::ClockEvent release_event;
    /// Writeback cycle of the last offloaded operation (in-order responses).
    int64_t last_done = -1;
    /// Latency of the divide being retired, computed from its operands.
    int div_latency = 0;
};
