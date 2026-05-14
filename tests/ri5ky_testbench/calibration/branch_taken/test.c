// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Calibration: a chain of always-taken `beq x0, x0, .Lnext` branches.
// Each taken branch flushes the IF-stage prefetch buffer, so the cost
// per branch is the instruction itself plus a one-cycle bubble while
// the prefetcher refills.
//
// Single straight-line block — no surrounding C loop. The branches are
// independent (only x0 is used), so any extra cycles come from the
// pipeline flush, not from data hazards.
//
// Run twice: once in fast mode (PCMR.active=0) and once in slow mode
// (PCMR.active=1).

#include "calib.h"

#define N_BRANCHES 128

// 8 chained always-taken branches. Each falls through to the next label
// after a 4-byte beq.
#define EIGHT_TAKEN_BRANCHES \
    "beq x0, x0, 1f\n" "1:" \
    "beq x0, x0, 2f\n" "2:" \
    "beq x0, x0, 3f\n" "3:" \
    "beq x0, x0, 4f\n" "4:" \
    "beq x0, x0, 5f\n" "5:" \
    "beq x0, x0, 6f\n" "6:" \
    "beq x0, x0, 7f\n" "7:" \
    "beq x0, x0, 8f\n" "8:"

#define THIRTY_TWO_BRANCHES \
    EIGHT_TAKEN_BRANCHES EIGHT_TAKEN_BRANCHES \
    EIGHT_TAKEN_BRANCHES EIGHT_TAKEN_BRANCHES

#define DO_THE_BRANCHES \
    THIRTY_TWO_BRANCHES THIRTY_TWO_BRANCHES \
    THIRTY_TWO_BRANCHES THIRTY_TWO_BRANCHES

static inline uint32_t time_block(void)
{
    uint32_t start = calib_cycles();
    __asm__ volatile (DO_THE_BRANCHES :::);
    uint32_t end = calib_cycles();
    return end - start;
}

static inline uint32_t time_block_pcer(calib_pccr_t *before,
                                       calib_pccr_t *after)
{
    uint32_t start = calib_cycles();
    calib_pccr_read(before);
    __asm__ volatile (DO_THE_BRANCHES :::);
    calib_pccr_read(after);
    uint32_t end = calib_cycles();
    return end - start;
}

int main(void)
{
    calib_disable_pccr();
    uint32_t fast_cycles = time_block();

    calib_enable_pccr();
    uint32_t slow_cycles = time_block();
    calib_pccr_t before, after;
    time_block_pcer(&before, &after);

    CALIB_REPORT("branch_taken_fastmode", N_BRANCHES, fast_cycles);
    CALIB_REPORT("branch_taken_slowmode", N_BRANCHES, slow_cycles);
    CALIB_PCER_REPORT("branch_taken", before, after);
    return 0;
}
