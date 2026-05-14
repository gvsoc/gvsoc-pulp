// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Calibration: each load is followed by an instruction that consumes the
// load's destination register. The scoreboard parks the dest reg for one
// extra cycle past the response (load-use 1-cycle bubble), so each pair
// should retire in 3 cycles: load + 1-cycle stall + dependent addi.
//
// Single straight-line block of N pairs — no surrounding C loop.
//
// Run twice: once in fast mode (PCMR.active=0) and once in slow mode
// (PCMR.active=1).

#include "calib.h"

#define N_PAIRS 128

static volatile uint32_t buf[8] __attribute__((aligned(64)));

#define EIGHT_PAIRS \
    "lw   t0,  0(a0)\n"   "addi t0, t0, 1\n" \
    "lw   t1,  4(a0)\n"   "addi t1, t1, 1\n" \
    "lw   t2,  8(a0)\n"   "addi t2, t2, 1\n" \
    "lw   t3, 12(a0)\n"   "addi t3, t3, 1\n" \
    "lw   t4, 16(a0)\n"   "addi t4, t4, 1\n" \
    "lw   t5, 20(a0)\n"   "addi t5, t5, 1\n" \
    "lw   t6, 24(a0)\n"   "addi t6, t6, 1\n" \
    "lw   a1, 28(a0)\n"   "addi a1, a1, 1\n"

#define THIRTY_TWO_PAIRS  EIGHT_PAIRS EIGHT_PAIRS EIGHT_PAIRS EIGHT_PAIRS

#define DO_THE_PAIRS \
    THIRTY_TWO_PAIRS THIRTY_TWO_PAIRS \
    THIRTY_TWO_PAIRS THIRTY_TWO_PAIRS

static inline uint32_t time_block(volatile uint32_t *base)
{
    register uint32_t *p __asm__("a0") = (uint32_t *)base;
    uint32_t start = calib_cycles();
    __asm__ volatile (
        DO_THE_PAIRS
        : : "r"(p)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a1", "memory"
    );
    uint32_t end = calib_cycles();
    return end - start;
}

static inline uint32_t time_block_pcer(volatile uint32_t *base,
                                       calib_pccr_t *before,
                                       calib_pccr_t *after)
{
    register uint32_t *p __asm__("a0") = (uint32_t *)base;
    uint32_t start = calib_cycles();
    calib_pccr_read(before);
    __asm__ volatile (
        DO_THE_PAIRS
        : : "r"(p)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a1", "memory"
    );
    calib_pccr_read(after);
    uint32_t end = calib_cycles();
    return end - start;
}

int main(void)
{
    for (int i = 0; i < 8; i++) buf[i] = 0;

    calib_disable_pccr();
    uint32_t fast_cycles = time_block(buf);

    calib_enable_pccr();
    uint32_t slow_cycles = time_block(buf);
    calib_pccr_t before, after;
    time_block_pcer(buf, &before, &after);

    CALIB_REPORT("load_use_fastmode", N_PAIRS, fast_cycles);
    CALIB_REPORT("load_use_slowmode", N_PAIRS, slow_cycles);
    CALIB_PCER_REPORT("load_use", before, after);
    return 0;
}
