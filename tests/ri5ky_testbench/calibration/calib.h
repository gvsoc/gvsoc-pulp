// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Tiny calibration helpers — read a free-running cycle counter around a
// code region and report the cycle delta in a machine-parsable form.
//
// Each test is run in two variants in main():
//
//   1. "slow_mode" — PCMR.active is set, so PCCR/PCER count events. On
//      GVSoC this also disables Ri5kyExec's fast path, forcing the slow
//      cycle-accurate executor (the only one that fires event_instr_account
//      and event_retire_account).
//
//   2. "fast_mode" — PCMR.active is cleared. On GVSoC the core runs in
//      the fast (functional) path; the timing hooks still fire from
//      jalr_exec_common / branch handlers, so cycle counts should remain
//      accurate after the jr_stall fix landed.
//
// We always read cycles from the testbench MMIO at +0x8 (CYCLE_LO), not
// from PCCR — PCCR only accumulates when PCMR.active is 1, which would
// make the fast-mode variant unable to measure anything.
//
// The Verilator testbench routes putchar through the same MMIO peripheral,
// so the same code prints on both GVSoC and the Verilator simulator.

#pragma once

#include <stdio.h>
#include <stdint.h>

#define CALIB_MMIO_BASE        0x10000000u
#define CALIB_MMIO_CYCLE_LO    (*(volatile uint32_t *)(CALIB_MMIO_BASE + 0x8u))

static inline uint32_t calib_cycles(void)
{
    return CALIB_MMIO_CYCLE_LO;
}

// Enable PCCR / PCMR.active. On GVSoC this also forces the slow exec
// path (Ri5kyExec::can_switch_to_fast_mode returns false when counters
// are enabled). All 12 PCER event-enable bits are set so every PCCR
// counter is live (cycles, instr, ld_stall, jr_stall, imiss, ld, st,
// jump, branch, btaken, rvc, elw — see RI5CY riscv_cs_registers.sv:
// 1097-1108). PCER=0xCC0, PCMR=0xCC1.
#define CALIB_PCER_ALL_MASK 0xFFFu

static inline void calib_enable_pccr(void)
{
    uint32_t mask = CALIB_PCER_ALL_MASK;
    uint32_t one = 1;
    __asm__ volatile ("csrw 0xCC0, %0" : : "r"(mask));
    __asm__ volatile ("csrw 0xCC1, %0" : : "r"(one));
}

// Clear PCMR.active so the GVSoC core runs in fast mode. PCCR stops
// accumulating but the MMIO cycle counter keeps ticking.
static inline void calib_disable_pccr(void)
{
    uint32_t zero = 0;
    __asm__ volatile ("csrw 0xCC1, %0" : : "r"(zero));
}

// All 12 RI5CY performance counters, in PCER bit-index order (matches
// pulpos/perf.h and ri5ky/csr.hpp).
#define CALIB_NB_PCCR 12
typedef struct {
    uint32_t v[CALIB_NB_PCCR];
} calib_pccr_t;

static inline void calib_pccr_read(calib_pccr_t *out)
{
    __asm__ volatile ("csrr %0, 0x780" : "=r"(out->v[0]));   // cycles
    __asm__ volatile ("csrr %0, 0x781" : "=r"(out->v[1]));   // instr
    __asm__ volatile ("csrr %0, 0x782" : "=r"(out->v[2]));   // ld_stall
    __asm__ volatile ("csrr %0, 0x783" : "=r"(out->v[3]));   // jr_stall
    __asm__ volatile ("csrr %0, 0x784" : "=r"(out->v[4]));   // imiss
    __asm__ volatile ("csrr %0, 0x785" : "=r"(out->v[5]));   // ld
    __asm__ volatile ("csrr %0, 0x786" : "=r"(out->v[6]));   // st
    __asm__ volatile ("csrr %0, 0x787" : "=r"(out->v[7]));   // jump
    __asm__ volatile ("csrr %0, 0x788" : "=r"(out->v[8]));   // branch
    __asm__ volatile ("csrr %0, 0x789" : "=r"(out->v[9]));   // btaken
    __asm__ volatile ("csrr %0, 0x78A" : "=r"(out->v[10]));  // rvc
    __asm__ volatile ("csrr %0, 0x78B" : "=r"(out->v[11]));  // elw
}

#define CALIB_REPORT(name, iters, delta)                                   \
    do {                                                                    \
        printf("CALIB " name " cycles=%u iters=%u\n",                       \
               (unsigned)(delta), (unsigned)(iters));                       \
    } while (0)

// Emit a single CALIB_PCER line with the deltas of all 12 counters
// between two read-out points.
#define CALIB_PCER_REPORT(name, before, after)                                  \
    do {                                                                         \
        printf("CALIB_PCER " name                                                \
               " cycles=%u instr=%u ld_stall=%u jr_stall=%u"                     \
               " imiss=%u ld=%u st=%u jump=%u"                                   \
               " branch=%u btaken=%u rvc=%u elw=%u\n",                           \
            (unsigned)((after).v[0]  - (before).v[0]),                           \
            (unsigned)((after).v[1]  - (before).v[1]),                           \
            (unsigned)((after).v[2]  - (before).v[2]),                           \
            (unsigned)((after).v[3]  - (before).v[3]),                           \
            (unsigned)((after).v[4]  - (before).v[4]),                           \
            (unsigned)((after).v[5]  - (before).v[5]),                           \
            (unsigned)((after).v[6]  - (before).v[6]),                           \
            (unsigned)((after).v[7]  - (before).v[7]),                           \
            (unsigned)((after).v[8]  - (before).v[8]),                           \
            (unsigned)((after).v[9]  - (before).v[9]),                           \
            (unsigned)((after).v[10] - (before).v[10]),                          \
            (unsigned)((after).v[11] - (before).v[11]));                         \
    } while (0)
