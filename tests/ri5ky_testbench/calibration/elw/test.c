// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Calibration: p.elw (event load) against the asynchronous slow-memory
// region. A real event unit always answers asynchronously, so the slow
// region is modelled by Ri5kyAsyncMem (gvsoc) / slow_mem.sv (RTL): it
// grants the request and replies rvalid `slow_mem_latency` cycles later.
//
// RI5CY decodes any funct3==110 load as p.elw and drives the controller
// into ELW_EXE: the whole pipeline (IF/ID) is halted until the response
// arrives, so the elw can NOT overlap with following instructions and
// pays the full response latency. Every wasted cycle asserts
// perf_pipeline_stall_o, which feeds PCCR[11] — the ELW performance
// counter ("extra cycles from elw"). On GVSoC the elw clock-gates the
// core (busy_exit) for the same span, woken by the async response.
//
// p.elw is emitted with the generic .insn form (LOAD opcode 0x03, funct3
// 0x6) so it does not depend on assembler p.* mnemonic support:
//     .insn i 0x03, 0x6, rd, imm(rs1)   ==   p.elw rd, imm(rs1)
//
// Run twice: once in fast mode (PCMR.active=0) and once in slow mode
// (PCMR.active=1).

#include "calib.h"

#define N_ELW 64

// Event-unit accesses must hit the ASYNCHRONOUS region (0x5000_0000):
// p.elw's clock-gated park/wake path only engages against an async
// responder (grants, then replies later). The 0x4000_0000 region is the
// synchronous slow memory and would not exercise the park path.
#define SLOW_MEM_BASE ((volatile uint32_t *)0x50000000u)

#define EIGHT_ELW \
    ".insn i 0x03, 0x6, t0,  0(a0)\n" \
    ".insn i 0x03, 0x6, t1,  4(a0)\n" \
    ".insn i 0x03, 0x6, t2,  8(a0)\n" \
    ".insn i 0x03, 0x6, t3, 12(a0)\n" \
    ".insn i 0x03, 0x6, t4, 16(a0)\n" \
    ".insn i 0x03, 0x6, t5, 20(a0)\n" \
    ".insn i 0x03, 0x6, t6, 24(a0)\n" \
    ".insn i 0x03, 0x6, a1, 28(a0)\n"

#define DO_THE_ELW \
    EIGHT_ELW EIGHT_ELW EIGHT_ELW EIGHT_ELW \
    EIGHT_ELW EIGHT_ELW EIGHT_ELW EIGHT_ELW

static inline uint32_t time_block(volatile uint32_t *base)
{
    register uint32_t *p __asm__("a0") = (uint32_t *)base;
    uint32_t start = calib_cycles();
    __asm__ volatile (
        DO_THE_ELW
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
        DO_THE_ELW
        : : "r"(p)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a1", "memory"
    );
    calib_pccr_read(after);
    uint32_t end = calib_cycles();
    return end - start;
}

int main(void)
{
    calib_pccr_t before, after;

    calib_disable_pccr();
    uint32_t fast_cycles = time_block(SLOW_MEM_BASE);

    calib_enable_pccr();
    uint32_t slow_cycles = time_block(SLOW_MEM_BASE);
    time_block_pcer(SLOW_MEM_BASE, &before, &after);

    CALIB_REPORT("elw_fastmode", N_ELW, fast_cycles);
    CALIB_REPORT("elw_slowmode", N_ELW, slow_cycles);
    CALIB_PCER_REPORT("elw", before, after);
    return 0;
}
