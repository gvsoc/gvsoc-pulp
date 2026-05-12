// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Calibration: back-to-back `mulh` instructions. The RI5CY multiplier
// runs `mul` in a single cycle but takes the MULH path through a
// 4-state FSM (STEP0, STEP1, STEP2, FINISH) before a result is ready.
// Each MULH should therefore retire in ~5 cycles, while a regular MUL
// is 1 cycle.
//
// Two blocks, both measured: one with independent destinations
// (sweeping the temporaries — proves the FSM serialises issue
// structurally, not via a data hazard) and one with a chained
// dependency (each MULH reads the previous MULH's output — proves the
// model still produces the same cost when scoreboard dependency would
// also stall). On RI5CY both should land at the same ~5 cyc/MULH; if
// a future Ri5ky model regressed to a scoreboard-only stall policy
// the independent block would drop to ~1 cyc/MULH and the test would
// catch it.
//
// Each block is also run twice — once in fast mode (PCMR.active=0)
// and once in slow mode (PCMR.active=1).

#include "calib.h"

#define N_OPS 64

// ----- Independent: every MULH writes a distinct destination. -------
#define EIGHT_INDEP_MULH \
    "mulh t0, a0, a1\n" \
    "mulh t1, a0, a1\n" \
    "mulh t2, a0, a1\n" \
    "mulh t3, a0, a1\n" \
    "mulh t4, a0, a1\n" \
    "mulh t5, a0, a1\n" \
    "mulh t6, a0, a1\n" \
    "mulh a2, a0, a1\n"

#define THIRTY_TWO_INDEP_MULH \
    EIGHT_INDEP_MULH EIGHT_INDEP_MULH EIGHT_INDEP_MULH EIGHT_INDEP_MULH

// ----- Dependent: every MULH reads the previous MULH's output. ------
// All 64 MULHs write t0; each one consumes the t0 produced by the
// previous one. The first MULH still reads a0/a1 (no producer yet),
// but that's only one out of 64 so the per-MULH cost is dominated by
// the dependent steady state.
#define EIGHT_DEP_MULH \
    "mulh t0, t0, a1\n" \
    "mulh t0, t0, a1\n" \
    "mulh t0, t0, a1\n" \
    "mulh t0, t0, a1\n" \
    "mulh t0, t0, a1\n" \
    "mulh t0, t0, a1\n" \
    "mulh t0, t0, a1\n" \
    "mulh t0, t0, a1\n"

#define THIRTY_TWO_DEP_MULH \
    EIGHT_DEP_MULH EIGHT_DEP_MULH EIGHT_DEP_MULH EIGHT_DEP_MULH

static inline uint32_t time_indep(uint32_t a, uint32_t b)
{
    register uint32_t op_a __asm__("a0") = a;
    register uint32_t op_b __asm__("a1") = b;
    uint32_t start = calib_cycles();
    __asm__ volatile (
        THIRTY_TWO_INDEP_MULH THIRTY_TWO_INDEP_MULH
        : : "r"(op_a), "r"(op_b)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a2"
    );
    uint32_t end = calib_cycles();
    return end - start;
}

static inline uint32_t time_dep(uint32_t a, uint32_t b)
{
    register uint32_t op_a __asm__("a0") = a;
    register uint32_t op_b __asm__("a1") = b;
    uint32_t start = calib_cycles();
    __asm__ volatile (
        // Seed t0 with a0 so the first MULH has a meaningful input;
        // every subsequent MULH then chains through t0.
        "mv   t0, %0\n"
        THIRTY_TWO_DEP_MULH THIRTY_TWO_DEP_MULH
        : : "r"(op_a), "r"(op_b)
        : "t0"
    );
    uint32_t end = calib_cycles();
    return end - start;
}

int main(void)
{
    // Non-trivial operand values so the multiplier doesn't optimise.
    calib_disable_pccr();
    uint32_t indep_fast = time_indep(0x12345678, 0x87654321);
    uint32_t dep_fast   = time_dep  (0x12345678, 0x87654321);

    calib_enable_pccr();
    uint32_t indep_slow = time_indep(0x12345678, 0x87654321);
    uint32_t dep_slow   = time_dep  (0x12345678, 0x87654321);

    CALIB_REPORT("mulh_latency_indep_fastmode", N_OPS, indep_fast);
    CALIB_REPORT("mulh_latency_indep_slowmode", N_OPS, indep_slow);
    CALIB_REPORT("mulh_latency_dep_fastmode",   N_OPS, dep_fast);
    CALIB_REPORT("mulh_latency_dep_slowmode",   N_OPS, dep_slow);
    return 0;
}
