// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Calibration: word loads from an address that crosses a 4-byte
// boundary. RI5CY's LSU detects the misaligned access (`data_misaligned`
// in load_store_unit.sv) and splits it into two word accesses, with the
// controller raising `misaligned_stall_o` until the second beat lands.
// Each misaligned `lw` therefore costs ~2 × the underlying memory
// response latency.
//
// Two regions × two modes — 4 measurements total:
//   1. fast mem  (0x0..0x100000, latency=1) → ~2 cyc/lw
//   2. slow mem  (0x40000000..,  latency=5) → ~10 cyc/lw (5 per beat)
//   × each measured with PCMR off (GVSoC fast exec) and PCMR on (slow exec).

#include "calib.h"

#define N_LOADS 128

// Buffer in fast mem (0x0..0x100000 region). Offsets 1, 5, 9, 13...
// all straddle a 4-byte boundary.
static volatile uint8_t fast_buf[64] __attribute__((aligned(64)));

// Buffer in slow mem (0x4000_0000 region). Initialised at runtime.
#define SLOW_BUF ((volatile uint8_t *)0x40000040u)

#define EIGHT_MISALIGNED_LOADS \
    "lw t0,  1(a0)\n" \
    "lw t1,  5(a0)\n" \
    "lw t2,  9(a0)\n" \
    "lw t3, 13(a0)\n" \
    "lw t4, 17(a0)\n" \
    "lw t5, 21(a0)\n" \
    "lw t6, 25(a0)\n" \
    "lw a1, 29(a0)\n"

#define THIRTY_TWO_LOADS \
    EIGHT_MISALIGNED_LOADS EIGHT_MISALIGNED_LOADS \
    EIGHT_MISALIGNED_LOADS EIGHT_MISALIGNED_LOADS

#define DO_THE_LOADS \
    THIRTY_TWO_LOADS THIRTY_TWO_LOADS \
    THIRTY_TWO_LOADS THIRTY_TWO_LOADS

static inline uint32_t time_block(volatile uint8_t *base)
{
    register uint8_t *p __asm__("a0") = (uint8_t *)base;
    uint32_t start = calib_cycles();
    __asm__ volatile (
        DO_THE_LOADS
        : : "r"(p)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a1", "memory"
    );
    uint32_t end = calib_cycles();
    return end - start;
}

static inline uint32_t time_block_pcer(volatile uint8_t *base,
                                       calib_pccr_t *before,
                                       calib_pccr_t *after)
{
    register uint8_t *p __asm__("a0") = (uint8_t *)base;
    uint32_t start = calib_cycles();
    calib_pccr_read(before);
    __asm__ volatile (
        DO_THE_LOADS
        : : "r"(p)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a1", "memory"
    );
    calib_pccr_read(after);
    uint32_t end = calib_cycles();
    return end - start;
}

int main(void)
{
    for (int i = 0; i < 64; i++) fast_buf[i] = (uint8_t)i;
    for (int i = 0; i < 64; i++) SLOW_BUF[i]  = (uint8_t)i;

    calib_disable_pccr();
    uint32_t fastmem_fastmode = time_block(fast_buf);
    uint32_t slowmem_fastmode = time_block(SLOW_BUF);

    calib_enable_pccr();
    uint32_t fastmem_slowmode = time_block(fast_buf);
    uint32_t slowmem_slowmode = time_block(SLOW_BUF);
    calib_pccr_t pcer_before, pcer_after;
    time_block_pcer(fast_buf, &pcer_before, &pcer_after);

    CALIB_REPORT("misaligned_load_fastmem_fastmode", N_LOADS, fastmem_fastmode);
    CALIB_REPORT("misaligned_load_fastmem_slowmode", N_LOADS, fastmem_slowmode);
    CALIB_REPORT("misaligned_load_slowmem_fastmode", N_LOADS, slowmem_fastmode);
    CALIB_REPORT("misaligned_load_slowmem_slowmode", N_LOADS, slowmem_slowmode);
    // PCER captured for the fast-mem variant only (misaligned access pattern;
    // the slow-mem variant exercises the same instructions just with a
    // bigger memory latency).
    CALIB_PCER_REPORT("misaligned_load", pcer_before, pcer_after);
    return 0;
}
