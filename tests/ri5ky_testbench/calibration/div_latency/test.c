// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Calibration: back-to-back `div / divu / rem / remu` instructions.
// RI5CY's divider (riscv_alu_div.sv) is a counter-driven serial unit:
//   IDLE (1 cycle)
//   DIVIDE[N] (N cycles)
//   FINISH (1 cycle)
// where N is loaded from the ALU's leading-zero pre-shift on the
// divisor (riscv_alu.sv:1035-1036). Concretely:
//   - small divisors (mostly leading zeros) → many iterations
//   - large divisors (high MSB) → few iterations
//   - divisor == 0 is special-cased to run the maximum iteration count
//   - signed div shaves one cycle when the dividend is non-negative
//
// The follower instruction is held in ID for the whole FSM
// (riscv_ex_stage.sv:598-601 gates ex_ready_o on div_ready) regardless
// of any data dependency on the result, so we test both independent
// (alternating dest) and dependent (chained dest) chains for every
// divisor and expect them to land at the same per-op cost on RI5CY.
//
// Test cases (each measured twice — fast mode and slow mode):
//   * main  (b=0x9876)        — typical mid-range divisor (~16 iters)
//   * one   (b=1)             — maximum iteration count (~32 iters)
//   * msb   (b=0x40000000)    — small iteration count (~2 iters)
//   * zero  (b=0)             — special-cased path (zero divisor)
//   * divu  (b=0x9876)        — unsigned variant
//   * rem   (b=0x9876)        — remainder variant (same FSM)

#include "calib.h"

#define N_OPS 16

// ---------- independent: every div writes a fresh destination -------
#define FOUR_INDEP_DIV(OP) \
    OP " t0, a0, a1\n" \
    OP " t1, a0, a1\n" \
    OP " t2, a0, a1\n" \
    OP " t3, a0, a1\n"

#define SIXTEEN_INDEP_DIV(OP) \
    FOUR_INDEP_DIV(OP) FOUR_INDEP_DIV(OP) \
    FOUR_INDEP_DIV(OP) FOUR_INDEP_DIV(OP)

// ---------- dependent: each div reads the previous div's result -----
// All 16 divs write t0; each consumes the t0 produced by the previous.
#define FOUR_DEP_DIV(OP) \
    OP " t0, t0, a1\n" \
    OP " t0, t0, a1\n" \
    OP " t0, t0, a1\n" \
    OP " t0, t0, a1\n"

#define SIXTEEN_DEP_DIV(OP) \
    FOUR_DEP_DIV(OP) FOUR_DEP_DIV(OP) \
    FOUR_DEP_DIV(OP) FOUR_DEP_DIV(OP)

#define TIME_INDEP(NAME, OP, A_VAL, B_VAL)                                   \
    static inline uint32_t time_indep_##NAME(void)                            \
    {                                                                          \
        register uint32_t op_a __asm__("a0") = (A_VAL);                       \
        register uint32_t op_b __asm__("a1") = (B_VAL);                       \
        uint32_t start = calib_cycles();                                      \
        __asm__ volatile (                                                     \
            SIXTEEN_INDEP_DIV(OP)                                             \
            : : "r"(op_a), "r"(op_b)                                          \
            : "t0", "t1", "t2", "t3"                                          \
        );                                                                     \
        uint32_t end = calib_cycles();                                        \
        return end - start;                                                   \
    }

#define TIME_DEP(NAME, OP, A_VAL, B_VAL)                                     \
    static inline uint32_t time_dep_##NAME(void)                              \
    {                                                                          \
        register uint32_t op_a __asm__("a0") = (A_VAL);                       \
        register uint32_t op_b __asm__("a1") = (B_VAL);                       \
        uint32_t start = calib_cycles();                                      \
        __asm__ volatile (                                                     \
            "mv t0, %0\n"                                                     \
            SIXTEEN_DEP_DIV(OP)                                               \
            : : "r"(op_a), "r"(op_b)                                          \
            : "t0"                                                            \
        );                                                                     \
        uint32_t end = calib_cycles();                                        \
        return end - start;                                                   \
    }

TIME_INDEP(main,  "div",  0x12345678, 0x9876)
TIME_DEP  (main,  "div",  0x12345678, 0x9876)
TIME_INDEP(one,   "div",  0x12345678, 0x00000001)
TIME_DEP  (one,   "div",  0x12345678, 0x00000001)
TIME_INDEP(msb,   "div",  0x12345678, 0x40000000)
TIME_DEP  (msb,   "div",  0x12345678, 0x40000000)
TIME_INDEP(zero,  "div",  0x12345678, 0x00000000)
TIME_DEP  (zero,  "div",  0x12345678, 0x00000000)
TIME_INDEP(divu,  "divu", 0x12345678, 0x9876)
TIME_INDEP(rem,   "rem",  0x12345678, 0x9876)

#define REPORT(MODE, NAME) \
    CALIB_REPORT("div_latency_" NAME "_" MODE, N_OPS, NAME##_cycles)

int main(void)
{
    uint32_t main_indep_cycles, main_dep_cycles;
    uint32_t one_indep_cycles,  one_dep_cycles;
    uint32_t msb_indep_cycles,  msb_dep_cycles;
    uint32_t zero_indep_cycles, zero_dep_cycles;
    uint32_t divu_indep_cycles, rem_indep_cycles;

    // ---------- fast mode (PCMR.active = 0) ----------
    calib_disable_pccr();
    main_indep_cycles = time_indep_main();
    main_dep_cycles   = time_dep_main();
    one_indep_cycles  = time_indep_one();
    one_dep_cycles    = time_dep_one();
    msb_indep_cycles  = time_indep_msb();
    msb_dep_cycles    = time_dep_msb();
    zero_indep_cycles = time_indep_zero();
    zero_dep_cycles   = time_dep_zero();
    divu_indep_cycles = time_indep_divu();
    rem_indep_cycles  = time_indep_rem();

    CALIB_REPORT("div_latency_main_indep_fastmode", N_OPS, main_indep_cycles);
    CALIB_REPORT("div_latency_main_dep_fastmode",   N_OPS, main_dep_cycles);
    CALIB_REPORT("div_latency_one_indep_fastmode",  N_OPS, one_indep_cycles);
    CALIB_REPORT("div_latency_one_dep_fastmode",    N_OPS, one_dep_cycles);
    CALIB_REPORT("div_latency_msb_indep_fastmode",  N_OPS, msb_indep_cycles);
    CALIB_REPORT("div_latency_msb_dep_fastmode",    N_OPS, msb_dep_cycles);
    CALIB_REPORT("div_latency_zero_indep_fastmode", N_OPS, zero_indep_cycles);
    CALIB_REPORT("div_latency_zero_dep_fastmode",   N_OPS, zero_dep_cycles);
    CALIB_REPORT("div_latency_divu_indep_fastmode", N_OPS, divu_indep_cycles);
    CALIB_REPORT("div_latency_rem_indep_fastmode",  N_OPS, rem_indep_cycles);

    // ---------- slow mode (PCMR.active = 1) ----------
    calib_enable_pccr();
    main_indep_cycles = time_indep_main();
    main_dep_cycles   = time_dep_main();
    one_indep_cycles  = time_indep_one();
    one_dep_cycles    = time_dep_one();
    msb_indep_cycles  = time_indep_msb();
    msb_dep_cycles    = time_dep_msb();
    zero_indep_cycles = time_indep_zero();
    zero_dep_cycles   = time_dep_zero();
    divu_indep_cycles = time_indep_divu();
    rem_indep_cycles  = time_indep_rem();

    CALIB_REPORT("div_latency_main_indep_slowmode", N_OPS, main_indep_cycles);
    CALIB_REPORT("div_latency_main_dep_slowmode",   N_OPS, main_dep_cycles);
    CALIB_REPORT("div_latency_one_indep_slowmode",  N_OPS, one_indep_cycles);
    CALIB_REPORT("div_latency_one_dep_slowmode",    N_OPS, one_dep_cycles);
    CALIB_REPORT("div_latency_msb_indep_slowmode",  N_OPS, msb_indep_cycles);
    CALIB_REPORT("div_latency_msb_dep_slowmode",    N_OPS, msb_dep_cycles);
    CALIB_REPORT("div_latency_zero_indep_slowmode", N_OPS, zero_indep_cycles);
    CALIB_REPORT("div_latency_zero_dep_slowmode",   N_OPS, zero_dep_cycles);
    CALIB_REPORT("div_latency_divu_indep_slowmode", N_OPS, divu_indep_cycles);
    CALIB_REPORT("div_latency_rem_indep_slowmode",  N_OPS, rem_indep_cycles);

    return 0;
}
