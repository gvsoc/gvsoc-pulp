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
// are enabled). PCER=0xCC0, PCMR=0xCC1.
static inline void calib_enable_pccr(void)
{
    uint32_t one = 1;
    __asm__ volatile ("csrw 0xCC0, %0" : : "r"(one));
    __asm__ volatile ("csrw 0xCC1, %0" : : "r"(one));
}

// Clear PCMR.active so the GVSoC core runs in fast mode. PCCR stops
// accumulating but the MMIO cycle counter keeps ticking.
static inline void calib_disable_pccr(void)
{
    uint32_t zero = 0;
    __asm__ volatile ("csrw 0xCC1, %0" : : "r"(zero));
}

#define CALIB_REPORT(name, iters, delta)                                   \
    do {                                                                    \
        printf("CALIB " name " cycles=%u iters=%u\n",                       \
               (unsigned)(delta), (unsigned)(iters));                       \
    } while (0)
