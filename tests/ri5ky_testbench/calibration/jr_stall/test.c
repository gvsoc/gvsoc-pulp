// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Calibration: a chain of `auipc + addi + jalr` where the `addi` is the
// producer of the register read by `jalr`. RI5CY's controller raises
// `jr_stall_o` whenever a BRANCH_JALR sees its source register being
// written from EX/WB/ALU-forward, so each triplet pays the dependency
// bubble *on top of* the taken-jump pipeline flush.
//
// Each triplet:
//
//   auipc t0, 0            ; t0 = current pc
//   addi  t0, t0, 12       ; t0 += 12 → next label
//   jalr  zero, 0(t0)      ; depends on t0 → jr_stall
//
// Run twice: once in fast mode (PCMR.active=0) and once in slow mode
// (PCMR.active=1). Both must match the RTL — in fast mode this requires
// Ri5kyEvents::event_retire_account to fire from exec_instr so the
// jr_stall hazard can be detected.

#include "calib.h"

#define N_TRIPLETS 32

// One block = one (auipc, addi, jalr) trio. The `jalr` always jumps to
// label `Nf` which is immediately after the jalr — 12 bytes past the
// auipc (auipc 4 + addi 4 + jalr 4 = 12). Each iteration just falls
// through to the next.
//
// `.option norvc` forces the assembler to keep `addi` at 4 bytes; it
// would otherwise pick `c.addi`, shrinking the block to 10 bytes and
// landing the jalr target in the middle of the next auipc.
#define ONE_JR_TRIPLET(N) \
    ".option push\n"              \
    ".option norvc\n"             \
    "auipc t0, 0\n"               \
    "addi  t0, t0, 12\n"          \
    "jalr  zero, 0(t0)\n"         \
    ".option pop\n"               \
    #N ":"

#define EIGHT_JR_TRIPLETS \
    ONE_JR_TRIPLET(1) ONE_JR_TRIPLET(2) \
    ONE_JR_TRIPLET(3) ONE_JR_TRIPLET(4) \
    ONE_JR_TRIPLET(5) ONE_JR_TRIPLET(6) \
    ONE_JR_TRIPLET(7) ONE_JR_TRIPLET(8)

static inline uint32_t time_block(void)
{
    uint32_t start = calib_cycles();
    __asm__ volatile (
        EIGHT_JR_TRIPLETS EIGHT_JR_TRIPLETS
        EIGHT_JR_TRIPLETS EIGHT_JR_TRIPLETS
        : : : "t0"
    );
    uint32_t end = calib_cycles();
    return end - start;
}

int main(void)
{
    calib_disable_pccr();
    uint32_t fast_cycles = time_block();

    calib_enable_pccr();
    uint32_t slow_cycles = time_block();

    CALIB_REPORT("jr_stall_fastmode", N_TRIPLETS, fast_cycles);
    CALIB_REPORT("jr_stall_slowmode", N_TRIPLETS, slow_cycles);
    return 0;
}
