// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Calibration: load from the slow memory region (configured with extra
// response latency in ri5ky_testbench_config.py).
//
// Each trio in the inner block:
//
//   lw   t0, 0(a0)        ; issued, response delayed by L cycles
//   addi a1, a1, 1        ; independent — fills the 1-instr post-load window
//   addi t0, t0, 1        ; depends on t0 — must wait for the response
//
// Expected cost per trio: 3 + L cycles (the load + the 1 free instr + the
// stall waiting for the response + the dependent addi, minus the 1 cycle
// the independent instr already burned during the stall).
//
// Single straight-line block of N trios — no surrounding C loop.
//
// Run twice: once in fast mode (PCMR.active=0) and once in slow mode
// (PCMR.active=1).

#include "calib.h"

#define N_TRIOS 64

#define SLOW_MEM_BASE ((volatile uint32_t *)0x40000000u)

#define FOUR_TRIOS \
    "lw   t0,  0(a0)\n"   "addi a1, a1, 1\n"   "addi t0, t0, 1\n" \
    "lw   t1,  4(a0)\n"   "addi a1, a1, 1\n"   "addi t1, t1, 1\n" \
    "lw   t2,  8(a0)\n"   "addi a1, a1, 1\n"   "addi t2, t2, 1\n" \
    "lw   t3, 12(a0)\n"   "addi a1, a1, 1\n"   "addi t3, t3, 1\n"

#define SIXTEEN_TRIOS  FOUR_TRIOS FOUR_TRIOS FOUR_TRIOS FOUR_TRIOS

#define DO_THE_TRIOS \
    SIXTEEN_TRIOS SIXTEEN_TRIOS \
    SIXTEEN_TRIOS SIXTEEN_TRIOS

static inline uint32_t time_block(volatile uint32_t *base)
{
    register uint32_t *p __asm__("a0") = (uint32_t *)base;
    uint32_t start = calib_cycles();
    __asm__ volatile (
        DO_THE_TRIOS
        : : "r"(p)
        : "t0", "t1", "t2", "t3", "a1", "memory"
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
        DO_THE_TRIOS
        : : "r"(p)
        : "t0", "t1", "t2", "t3", "a1", "memory"
    );
    calib_pccr_read(after);
    uint32_t end = calib_cycles();
    return end - start;
}

int main(void)
{
    for (int i = 0; i < 16; i++) SLOW_MEM_BASE[i] = 0;

    calib_disable_pccr();
    uint32_t fast_cycles = time_block(SLOW_MEM_BASE);

    calib_enable_pccr();
    uint32_t slow_cycles = time_block(SLOW_MEM_BASE);
    calib_pccr_t before, after;
    time_block_pcer(SLOW_MEM_BASE, &before, &after);

    CALIB_REPORT("delayed_load_fastmode", N_TRIOS, fast_cycles);
    CALIB_REPORT("delayed_load_slowmode", N_TRIOS, slow_cycles);
    CALIB_PCER_REPORT("delayed_load", before, after);
    return 0;
}
