// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Calibration: back-to-back independent loads with the load result never
// being consumed in the next cycle. With a 1-cycle memory response and
// no scoreboard dependency, ri5ky should pipeline one load per cycle.
//
// A single straight-line block of N loads — no surrounding C loop — so
// reported cycles divided by N is the per-load cost. We sweep through the
// 8 caller-saved temporaries so no instruction depends on the previous.
//
// Run twice: once in fast mode (PCMR.active=0, GVSoC functional exec),
// once in slow mode (PCMR.active=1, GVSoC cycle-accurate exec).

#include "calib.h"

#define N_LOADS 256

static volatile uint32_t buf[8] __attribute__((aligned(64)));

#define EIGHT_LOADS \
    "lw t0,  0(a0)\n" \
    "lw t1,  4(a0)\n" \
    "lw t2,  8(a0)\n" \
    "lw t3, 12(a0)\n" \
    "lw t4, 16(a0)\n" \
    "lw t5, 20(a0)\n" \
    "lw t6, 24(a0)\n" \
    "lw a1, 28(a0)\n"

#define THIRTY_TWO_LOADS  EIGHT_LOADS EIGHT_LOADS EIGHT_LOADS EIGHT_LOADS

static inline uint32_t time_block(volatile uint32_t *base)
{
    register uint32_t *p __asm__("a0") = (uint32_t *)base;
    uint32_t start = calib_cycles();
    __asm__ volatile (
        // 256 loads, fully unrolled, no branches in between.
        THIRTY_TWO_LOADS THIRTY_TWO_LOADS
        THIRTY_TWO_LOADS THIRTY_TWO_LOADS
        THIRTY_TWO_LOADS THIRTY_TWO_LOADS
        THIRTY_TWO_LOADS THIRTY_TWO_LOADS
        : : "r"(p)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a1", "memory"
    );
    uint32_t end = calib_cycles();
    return end - start;
}

int main(void)
{
    for (int i = 0; i < 8; i++) buf[i] = (uint32_t)i;

    calib_disable_pccr();
    uint32_t fast_cycles = time_block(buf);

    calib_enable_pccr();
    uint32_t slow_cycles = time_block(buf);

    CALIB_REPORT("pipelined_loads_fastmode", N_LOADS, fast_cycles);
    CALIB_REPORT("pipelined_loads_slowmode", N_LOADS, slow_cycles);
    return 0;
}
