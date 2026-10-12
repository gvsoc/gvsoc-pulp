/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
 * Copyright (C) 2026 Fondazione Chips-IT
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Nazareno Bruschi, UniBo <nazareno.bruschi@unibo.it>
 *          Germain Haugou, GreenWaves Technologies (germain.haugou@greenwaves-technologies.com)
 *          Marco Paci, Fondazione Chips-IT (marco.paci@chips.it)
 */

/* CORE-V handlers of gvsoc-core (models/cpu/iss/include/isa/corev.hpp and pulp_v2.hpp),
 * with the semantics of the CV32E40P RTL. Only the CV32E40P component includes it. */

#ifndef __CPU_ISS_V2_CORES_CV32E40P_ISA_COREV_HPP
#define __CPU_ISS_V2_CORES_CV32E40P_ISA_COREV_HPP

/* Subset headers are included in subset order, possibly before rv32i.hpp,
 * so this one includes the macros itself. */
#include "cpu/iss/include/isa_lib/int.h"
#include "cpu/iss_v2/include/isa_lib/macros.h"
#include "cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp"

#define COREV_HWLOOP_LPSTART0 0
#define COREV_HWLOOP_LPEND0 1
#define COREV_HWLOOP_LPCOUNT0 2

#define COREV_HWLOOP_LPSTART1 4
#define COREV_HWLOOP_LPEND1 5
#define COREV_HWLOOP_LPCOUNT1 6

#define COREV_HWLOOP_LPSTART(x) (COREV_HWLOOP_LPSTART0 + (x)*4)
#define COREV_HWLOOP_LPEND(x) (COREV_HWLOOP_LPEND0 + (x)*4)
#define COREV_HWLOOP_LPCOUNT(x) (COREV_HWLOOP_LPCOUNT0 + (x)*4)

/* Hardware-loop setters. Bits [1:0] of lpstart and lpend are 0. The Hwloop
 * module loops back after the last instruction of the body, so it keeps
 * LPEND - 4. The CSR reads LPEND from Cv32e40pCsr::hwloop_lpend, which is 0
 * for a loop never programmed. */
static inline void corev_hwloop_cosim_csr(Iss *iss, iss_reg_t address)
{
    if (iss->exec.cosim->enabled())
    {
        iss->exec.cosim->csr(address, CV32E40P_COSIM_CSR_INSN);
    }
}

static inline void corev_hwloop_set_start(Iss *iss, iss_insn_t *insn, int index, iss_reg_t start)
{
    iss->hwloop.set_start(index, start & ~(iss_reg_t)0x3);
    corev_hwloop_cosim_csr(iss, 0xCC0 + index * 4);
}

static inline void corev_hwloop_set_end(Iss *iss, iss_insn_t *insn, int index, iss_reg_t end)
{
    iss->csr.hwloop_lpend[index] = end & ~(iss_reg_t)0x3;
    iss->hwloop.set_end(index, (end & ~(iss_reg_t)0x3) - 4);
    corev_hwloop_cosim_csr(iss, 0xCC1 + index * 4);
}

static inline void corev_hwloop_set_count(Iss *iss, iss_insn_t *insn, int index, iss_reg_t count)
{
    iss->hwloop.set_count(index, count);
    corev_hwloop_cosim_csr(iss, 0xCC2 + index * 4);
}

static inline void corev_hwloop_set_all(Iss *iss, iss_insn_t *insn, int index, iss_reg_t start, iss_reg_t end, iss_reg_t count)
{
    corev_hwloop_set_end(iss, insn, index, end);
    corev_hwloop_set_start(iss, insn, index, start);
    corev_hwloop_set_count(iss, insn, index, count);
}

static inline iss_reg_t cv_clipr_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // rs2 & 0x7FFFFFFF (instruction_set_extensions.rst).
    int high = REG_GET(1) & 0x7fffffff;
    int low = -high - 1;
    REG_SET(0, LIB_CALL3(lib_CLIP, REG_GET(0), low, high));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_clipur_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // rs2 & 0x7FFFFFFF (instruction_set_extensions.rst).
    int low = 0;
    int high = REG_GET(1) & 0x7fffffff;
    REG_SET(0, LIB_CALL3(lib_CLIP, REG_GET(0), low, high));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_bclr_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL2(lib_BCLR, REG_GET(0), ((1ULL << width) - 1) << shift));

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_bclrr_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = ((REG_GET(1) >> 5) & 0x1f) + 1;
    int shift = REG_GET(1) & 0x1f;
    REG_SET(0, LIB_CALL2(lib_BCLR, REG_GET(0), ((1ULL << width) - 1) << shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_extract_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL3(lib_BEXTRACT, REG_GET(0), width, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_extractu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL3(lib_BEXTRACTU, REG_GET(0), ((1ULL << width) - 1) << shift, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_extractr_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = ((REG_GET(1) >> 5) & 0x1f) + 1;
    int shift = REG_GET(1) & 0x1f;
    REG_SET(0, LIB_CALL3(lib_BEXTRACT, REG_GET(0), width, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_extractur_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = ((REG_GET(1) >> 5) & 0x1f) + 1;
    int shift = REG_GET(1) & 0x1f;
    REG_SET(0, LIB_CALL3(lib_BEXTRACTU, REG_GET(0), ((1ULL << width) - 1) << shift, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_insert_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL4(lib_BINSERT, REG_GET(0), REG_GET(1), ((1ULL << width) - 1) << shift, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_insertr_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = ((REG_GET(2) >> 5) & 0x1F) + 1;
    int shift = REG_GET(2) & 0x1F;
    REG_SET(0, LIB_CALL4(lib_BINSERT, REG_GET(0), REG_GET(1), ((1ULL << width) - 1) << shift, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_bset_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL2(lib_BSET, REG_GET(0), ((1ULL << (width)) - 1) << shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_bsetr_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = ((REG_GET(1) >> 5) & 0x1f) + 1;
    int shift = REG_GET(1) & 0x1f;
    REG_SET(0, LIB_CALL2(lib_BSET, REG_GET(0), ((1ULL << (width)) - 1) << shift));
    return iss_insn_next(iss, insn, pc);
}

// Variants of the isa_lib helpers where they differ from the RTL.
static inline uint32_t rev32_cv32e40p(uint32_t x)
{
    uint32_t r = 0;
    for (int i = 0; i < 32; i++) if ((x >> i) & 1) r |= 1u << (31 - i);
    return r;
}
// cv.bitrev reverses rs1, shifts it right by Is2 and reverses it back, then reverses it by
// groups of 1, 2 or 3 bits (Is3 = 0, 1, 2). lib_BITREV groups the bits differently.
static inline unsigned int lib_BITREV_cv32e40p(Iss *s, unsigned int input, unsigned int Is2, unsigned int Is3)
{
    uint32_t sh = rev32_cv32e40p(rev32_cv32e40p(input) >> (Is2 & 0x1f));
    uint32_t sel = Is3 & 0x3;
    uint32_t out = 0;
    if (sel == 1)      { for (int j = 0; j < 16; j++) out |= ((sh >> (31 - 2 * j - 1)) & 0x3u) << (2 * j); }
    else if (sel == 2) { for (int j = 0; j < 10; j++) out |= ((sh >> (31 - 3 * j - 2)) & 0x7u) << (3 * j); }
    else               { out = rev32_cv32e40p(sh); }
    return out;
}
// In cv.machhuRN and cv.macuRN the accumulation wraps at 32 bits, so the carry of the
// rounding add past bit 31 is lost before the shift. lib_MAC_*_NR_R computes it on 64 bits.
static inline unsigned int lib_MAC_ZH_ZH_NR_R_cv32e40p(Iss *s, unsigned int a, unsigned int b, unsigned int c, unsigned int shift)
{
    uint32_t result = (uint32_t)(a + ((b >> 16) & 0xffffu) * ((c >> 16) & 0xffffu));
    if (shift > 0) result = (uint32_t)(result + (1u << (shift - 1))) >> shift;
    return result;
}
static inline unsigned int lib_MAC_ZL_ZL_NR_R_cv32e40p(Iss *s, unsigned int a, unsigned int b, unsigned int c, unsigned int shift)
{
    uint32_t result = (uint32_t)(a + (b & 0xffffu) * (c & 0xffffu));
    if (shift > 0) result = (uint32_t)(result + (1u << (shift - 1))) >> shift;
    return result;
}
// In cv.macsRN and cv.machhsRN, a + product + rounding constant wraps at 32 bits before the
// arithmetic shift, as in the RTL multiplier.
static inline unsigned int lib_MAC_SL_SL_NR_R_cv32e40p(Iss *s, unsigned int a, unsigned int b, unsigned int c, unsigned int shift)
{
    uint32_t result = a + SL(b) * SL(c);
    if (shift > 0) result += 1u << (shift - 1);
    return ((int32_t)result) >> shift;
}
static inline unsigned int lib_MAC_SH_SH_NR_R_cv32e40p(Iss *s, unsigned int a, unsigned int b, unsigned int c, unsigned int shift)
{
    uint32_t result = a + SH(b) * SH(c);
    if (shift > 0) result += 1u << (shift - 1);
    return ((int32_t)result) >> shift;
}

static inline iss_reg_t cv_bitrev_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    REG_SET(0, LIB_CALL3(lib_BITREV_cv32e40p, REG_GET(0), UIM_GET(0), UIM_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

/* Handler of the p.bitrev label (isa_gen derives the name from the label). */
static inline iss_reg_t p_bitrev_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    return cv_bitrev_exec(iss, insn, pc);
}


/* lp.start and lp.end take the address from rs1 (lp_starti_exec and
 * lp_endi_exec, PC-relative, are below). */
static inline iss_reg_t lp_start_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    corev_hwloop_set_start(iss, insn, UIM_GET(0), REG_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t lp_end_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    corev_hwloop_set_end(iss, insn, UIM_GET(0), REG_GET(0));
    return iss_insn_next(iss, insn, pc);
}

/* Handlers of gvsoc-core pulp_v2.hpp. */

#define PULPV2_HWLOOP_LPSTART0 COREV_HWLOOP_LPSTART0
#define PULPV2_HWLOOP_LPEND0   COREV_HWLOOP_LPEND0
#define PULPV2_HWLOOP_LPCOUNT0 COREV_HWLOOP_LPCOUNT0
#define PULPV2_HWLOOP_LPSTART1 COREV_HWLOOP_LPSTART1
#define PULPV2_HWLOOP_LPEND1   COREV_HWLOOP_LPEND1
#define PULPV2_HWLOOP_LPCOUNT1 COREV_HWLOOP_LPCOUNT1
#define PULPV2_HWLOOP_LPSTART(x) (PULPV2_HWLOOP_LPSTART0 + (x)*4)
#define PULPV2_HWLOOP_LPEND(x)   (PULPV2_HWLOOP_LPEND0 + (x)*4)
#define PULPV2_HWLOOP_LPCOUNT(x)  (PULPV2_HWLOOP_LPCOUNT0 + (x)*4)

static inline iss_reg_t LB_RR_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.load_signed<int8_t>(insn, REG_GET(0) + REG_GET(1), 1, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LB_RR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    iss->regfile.memcheck_access_reg(REG_IN(1));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0) + REG_GET(1));
    iss->lsu.stack_access_check(REG_IN(1), REG_GET(0) + REG_GET(1));
    if (iss->lsu.load_signed_perf<int8_t>(insn, REG_GET(0) + REG_GET(1), 1, REG_OUT(0)))
    {
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LH_RR_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.load_signed<int16_t>(insn, REG_GET(0) + REG_GET(1), 2, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LH_RR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    iss->regfile.memcheck_access_reg(REG_IN(1));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0) + REG_GET(1));
    iss->lsu.stack_access_check(REG_IN(1), REG_GET(0) + REG_GET(1));
    if (iss->lsu.load_signed_perf<int16_t>(insn, REG_GET(0) + REG_GET(1), 2, REG_OUT(0)))
    {
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LW_RR_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.load<int32_t>(insn, REG_GET(0) + REG_GET(1), 4, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LW_RR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    iss->regfile.memcheck_access_reg(REG_IN(1));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0) + REG_GET(1));
    iss->lsu.stack_access_check(REG_IN(1), REG_GET(0) + REG_GET(1));
    if (iss->lsu.load_perf<uint32_t>(insn, REG_GET(0) + REG_GET(1), 4, REG_OUT(0)))
    {
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LBU_RR_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.load<int8_t>(insn, REG_GET(0) + REG_GET(1), 1, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LBU_RR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    iss->regfile.memcheck_access_reg(REG_IN(1));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0) + REG_GET(1));
    iss->lsu.stack_access_check(REG_IN(1), REG_GET(0) + REG_GET(1));
    if (iss->lsu.load_perf<uint8_t>(insn, REG_GET(0) + REG_GET(1), 1, REG_OUT(0)))
    {
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LHU_RR_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.load<int16_t>(insn, REG_GET(0) + REG_GET(1), 2, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LHU_RR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    iss->regfile.memcheck_access_reg(REG_IN(1));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0) + REG_GET(1));
    iss->lsu.stack_access_check(REG_IN(1), REG_GET(0) + REG_GET(1));
    if (iss->lsu.load_perf<uint16_t>(insn, REG_GET(0) + REG_GET(1), 2, REG_OUT(0)))
    {
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LB_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    if (iss->lsu.load_signed<int8_t>(insn, base, 1, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    /* When rd = rs1, the loaded value wins over the incremented address
     * (instruction_set_extensions.rst). */
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LB_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));

    iss_reg_t base = REG_GET(0);
    if (iss->lsu.load_signed_perf<int8_t>(insn, base, 1, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LH_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    if (iss->lsu.load_signed<int16_t>(insn, base, 2, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LH_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));

    iss_reg_t base = REG_GET(0);
    if (iss->lsu.load_signed_perf<int16_t>(insn, base, 2, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LW_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    if (iss->lsu.load_signed<int32_t>(insn, base, 4, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LW_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));

    iss_reg_t base = REG_GET(0);
    if (iss->lsu.load_signed_perf<int32_t>(insn, base, 4, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LBU_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    if (iss->lsu.load<int8_t>(insn, base, 1, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LBU_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));

    iss_reg_t base = REG_GET(0);
    iss->lsu.stack_access_check(REG_IN(0), base);
    if (iss->lsu.load_perf<uint8_t>(insn, base, 1, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LHU_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    if (iss->lsu.load<int16_t>(insn, base, 2, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LHU_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));

    iss_reg_t base = REG_GET(0);
    iss->lsu.stack_access_check(REG_IN(0), base);
    if (iss->lsu.load_perf<uint16_t>(insn, base, 2, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SB_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.store<uint8_t>(insn, REG_GET(0), 1, REG_IN(1)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    IN_REG_SET(0, REG_GET(0) + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SB_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0));
    if (iss->lsu.store_perf<uint8_t>(insn, REG_GET(0), 1, REG_IN(1)))
    {
        return pc;
    }
    IN_REG_SET(0, REG_GET(0) + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SH_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.store<uint16_t>(insn, REG_GET(0), 2, REG_IN(1)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    IN_REG_SET(0, REG_GET(0) + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SH_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0));
    if (iss->lsu.store_perf<uint16_t>(insn, REG_GET(0), 2, REG_IN(1)))
    {
        return pc;
    }
    IN_REG_SET(0, REG_GET(0) + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SW_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.store<uint32_t>(insn, REG_GET(0), 4, REG_IN(1)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    IN_REG_SET(0, REG_GET(0) + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SW_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0));
    if (iss->lsu.store_perf<uint32_t>(insn, REG_GET(0), 4, REG_IN(1)))
    {
        return pc;
    }
    IN_REG_SET(0, REG_GET(0) + SIM_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LB_RR_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    if (iss->lsu.load_signed<int8_t>(insn, base, 1, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LB_RR_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(1));

    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    if (iss->lsu.load_signed_perf<int8_t>(insn, base, 1, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LH_RR_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    if (iss->lsu.load_signed<int16_t>(insn, base, 2, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LH_RR_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(1));

    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    if (iss->lsu.load_signed_perf<int16_t>(insn, base, 2, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LW_RR_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    if (iss->lsu.load_signed<int32_t>(insn, base, 4, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LW_RR_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(1));

    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    if (iss->lsu.load_signed_perf<int32_t>(insn, base, 4, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LBU_RR_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    if (iss->lsu.load<uint8_t>(insn, base, 1, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LBU_RR_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(1));

    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    iss->lsu.stack_access_check(REG_IN(0), base);
    if (iss->lsu.load_perf<uint8_t>(insn, base, 1, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LHU_RR_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    if (iss->lsu.load<uint16_t>(insn, base, 2, REG_OUT(0)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t LHU_RR_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(1));

    iss_reg_t base = REG_GET(0);
    iss_reg_t offset = REG_GET(1);
    iss->lsu.stack_access_check(REG_IN(0), base);
    if (iss->lsu.load_perf<uint16_t>(insn, base, 2, REG_OUT(0)))
    {
        return pc;
    }
    if (REG_OUT(0) != REG_IN(0))
        IN_REG_SET(0, base + offset);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SB_RR_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t new_val = REG_GET(0) + REG_GET(2);
    if (iss->lsu.store<uint8_t>(insn, REG_GET(0), 1, REG_IN(1)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    IN_REG_SET(0, new_val);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SB_RR_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(2));

    iss_reg_t new_val = REG_GET(0) + REG_GET(2);
    iss->lsu.stack_access_check(REG_OUT(0), REG_GET(0));
    if (iss->lsu.store_perf<uint8_t>(insn, REG_GET(0), 1, REG_IN(1)))
    {
        return pc;
    }
    IN_REG_SET(0, new_val);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SH_RR_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t new_val = REG_GET(0) + REG_GET(2);
    if (iss->lsu.store<uint16_t>(insn, REG_GET(0), 2, REG_IN(1)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    IN_REG_SET(0, new_val);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SH_RR_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(2));

    iss_reg_t new_val = REG_GET(0) + REG_GET(2);
    iss->lsu.stack_access_check(REG_OUT(0), REG_GET(0));
    if (iss->lsu.store_perf<uint16_t>(insn, REG_GET(0), 2, REG_IN(1)))
    {
        return pc;
    }
    IN_REG_SET(0, new_val);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SW_RR_POSTINC_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t new_val = REG_GET(0) + REG_GET(2);
    if (iss->lsu.store<uint32_t>(insn, REG_GET(0), 4, REG_IN(1)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    IN_REG_SET(0, new_val);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SW_RR_POSTINC_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    // Since input register is incremented, whole register becomes invalid if any bit is invalid
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_IN(0), REG_IN(2));

    iss_reg_t new_val = REG_GET(0) + REG_GET(2);
    iss->lsu.stack_access_check(REG_OUT(0), REG_GET(0));
    if (iss->lsu.store_perf<uint32_t>(insn, REG_GET(0), 4, REG_IN(1)))
    {
        return pc;
    }
    IN_REG_SET(0, new_val);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_avgu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));

    REG_SET(0, LIB_CALL2(lib_AVGU, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_slet_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));

    REG_SET(0, (int32_t)REG_GET(0) <= (int32_t)REG_GET(1));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_sletu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));

    REG_SET(0, REG_GET(0) <= REG_GET(1));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_min_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));

    REG_SET(0, LIB_CALL2(lib_MINS, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_minu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));

    REG_SET(0, LIB_CALL2(lib_MINU, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_max_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));

    REG_SET(0, LIB_CALL2(lib_MAXS, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_maxu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));

    REG_SET(0, LIB_CALL2(lib_MAXU, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_ror_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (!iss->regfile.memcheck_get_valid(REG_IN(1)))
    {
        // If register containing the rotation is invalid, this is making the whole output
        // register invalid
        iss->regfile.memcheck_set_valid(REG_OUT(0), false);
    }
    else
    {
        // Otherwise, handle the bits separately
        iss->regfile.memcheck_set(REG_OUT(0),
            LIB_CALL2(lib_ROR, iss->regfile.memcheck_get(REG_IN(0)), REG_GET(1)));
    }

    REG_SET(0, LIB_CALL2(lib_ROR, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_ff1_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));

    REG_SET(0, LIB_CALL1(lib_FF1, REG_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_fl1_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));

    REG_SET(0, LIB_CALL1(lib_FL1, REG_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_clb_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));

    REG_SET(0, LIB_CALL1(lib_CLB, REG_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_cnt_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));

    REG_SET(0, LIB_CALL1(lib_CNT, REG_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_exths_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_set(REG_OUT(0),
        iss_get_signed_value(iss->regfile.memcheck_get(REG_IN(0)), 16));

    REG_SET(0, iss_get_signed_value(REG_GET(0), 16));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_exthz_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_set(REG_OUT(0),
        iss_get_field(iss->regfile.memcheck_get(REG_IN(0)), 0, 16) |
        (((1 << (ISS_REG_WIDTH - 16)) - 1) << 16));

    REG_SET(0, iss_get_field(REG_GET(0), 0, 16));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_extbs_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_set(REG_OUT(0),
        iss_get_signed_value(iss->regfile.memcheck_get(REG_IN(0)), 8));

    REG_SET(0, iss_get_signed_value(REG_GET(0), 8));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_extbz_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_set(REG_OUT(0),
        iss_get_field(iss->regfile.memcheck_get(REG_IN(0)), 0, 8) |
        (((1 << (ISS_REG_WIDTH - 8)) - 1) << 8));

    REG_SET(0, iss_get_field(REG_GET(0), 0, 8));
    return iss_insn_next(iss, insn, pc);
}

/* The pulp_v2.hpp names of the hardware-loop setters, for the handlers below. */

static inline void hwloop_set_start(Iss *iss, iss_insn_t *insn, int index, iss_reg_t start)
{
    corev_hwloop_set_start(iss, insn, index, start);
}

static inline void hwloop_set_end(Iss *iss, iss_insn_t *insn, int index, iss_reg_t end)
{
    corev_hwloop_set_end(iss, insn, index, end);
}

static inline void hwloop_set_count(Iss *iss, iss_insn_t *insn, int index, iss_reg_t count)
{
    corev_hwloop_set_count(iss, insn, index, count);
}

static inline void hwloop_set_all(Iss *iss, iss_insn_t *insn, int index, iss_reg_t start, iss_reg_t end, iss_reg_t count)
{
    corev_hwloop_set_all(iss, insn, index, start, end, count);
}

// The hardware-loop immediates are word offsets (halfword offsets in PULP v2).
#define COREV_HWLOOP_IMM_SHIFT 2

static inline iss_reg_t lp_starti_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    hwloop_set_start(iss, insn, UIM_GET(0), pc + (UIM_GET(1) << COREV_HWLOOP_IMM_SHIFT));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t lp_endi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    hwloop_set_end(iss, insn, UIM_GET(0), pc + (UIM_GET(1) << COREV_HWLOOP_IMM_SHIFT));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t lp_count_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_branch_reg(REG_IN(0));

    hwloop_set_count(iss, insn, UIM_GET(0), REG_GET(0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t lp_counti_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    hwloop_set_count(iss, insn, UIM_GET(0), UIM_GET(1));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t lp_setup_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_branch_reg(REG_IN(0));

    int index = UIM_GET(0);
    iss_reg_t count = REG_GET(0);
    iss_reg_t start = pc + insn->size;
    iss_reg_t end = pc + (UIM_GET(1) << COREV_HWLOOP_IMM_SHIFT);

    hwloop_set_all(iss, insn, index, start, end, count);

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t lp_setupi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int index = UIM_GET(0);
    iss_reg_t count = UIM_GET(1);
    iss_reg_t start = pc + insn->size;
    iss_reg_t end = pc + (UIM_GET(2) << COREV_HWLOOP_IMM_SHIFT);

    hwloop_set_all(iss, insn, index, start, end, count);

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_abs_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));

    REG_SET(0, LIB_CALL1(lib_ABS, REG_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SB_RR_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.store<uint8_t>(insn, REG_GET(0) + REG_GET(2), 1, REG_IN(1)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SB_RR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    iss->regfile.memcheck_access_reg(REG_IN(2));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0) + REG_GET(2));
    iss->lsu.stack_access_check(REG_IN(2), REG_GET(0) + REG_GET(2));
    if (iss->lsu.store_perf<uint8_t>(insn, REG_GET(0) + REG_GET(2), 1, REG_IN(1)))
    {
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SH_RR_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.store<uint16_t>(insn, REG_GET(0) + REG_GET(2), 2, REG_IN(1)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SH_RR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    iss->regfile.memcheck_access_reg(REG_IN(2));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0) + REG_GET(2));
    iss->lsu.stack_access_check(REG_IN(2), REG_GET(0) + REG_GET(2));
    if (iss->lsu.store_perf<uint16_t>(insn, REG_GET(0) + REG_GET(2), 2, REG_IN(1)))
    {
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SW_RR_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->lsu.store<uint32_t>(insn, REG_GET(0) + REG_GET(2), 4, REG_IN(1)))
    {
        // This returns true if the core didn't manage to do the access and is stalled.
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t SW_RR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // If address register is not valid, we are accessing random location, trigger
    // a memcheck fail
    iss->regfile.memcheck_access_reg(REG_IN(0));
    iss->regfile.memcheck_access_reg(REG_IN(2));

    iss->lsu.stack_access_check(REG_IN(0), REG_GET(0) + REG_GET(2));
    iss->lsu.stack_access_check(REG_IN(2), REG_GET(0) + REG_GET(2));
    if (iss->lsu.store_perf<uint32_t>(insn, REG_GET(0) + REG_GET(2), 4, REG_IN(1)))
    {
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}


static inline iss_reg_t p_elw_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // cv.elw (COREV_CLUSTER): a load whose park in the event unit puts the core
    // to sleep until the event comes, or until an interrupt, after which the
    // cv.elw is replayed (ELW_EXE, IRQ_FLUSH_ELW in cv32e40p_controller.sv).
    // Without an event unit the load completes at once, as in the RTL while the
    // cluster keeps the clock enabled. The replay after a debug request taken
    // right after it (DBG_FLUSH) is not modelled.
    iss->regfile.memcheck_access_reg(REG_IN(0));
    if (iss->lsu.elw(insn, REG_GET(0) + SIM_GET(0), 4, REG_OUT(0)))
    {
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

#define PV_OP_RS_EXEC(insn_name, lib_name)                                                         \
    static inline iss_reg_t pv_##insn_name##_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                  \
    {                                                                                              \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_int16_t_to_int32_t, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                         \
    }                                                                                              \
                                                                                                   \
    static inline iss_reg_t pv_##insn_name##_sc_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)               \
    {                                                                                              \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_int16_t_to_int32_t, REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                         \
    }                                                                                              \
                                                                                                   \
    static inline iss_reg_t pv_##insn_name##_sci_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)              \
    {                                                                                              \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_int16_t_to_int32_t, REG_GET(0), SIM_GET(0))); \
        return iss_insn_next(iss, insn, pc);                                                                         \
    }                                                                                              \
                                                                                                   \
    static inline iss_reg_t pv_##insn_name##_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                  \
    {                                                                                              \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_int8_t_to_int32_t, REG_GET(0), REG_GET(1)));     \
        return iss_insn_next(iss, insn, pc);                                                                         \
    }                                                                                              \
                                                                                                   \
    static inline iss_reg_t pv_##insn_name##_sc_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)               \
    {                                                                                              \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_int8_t_to_int32_t, REG_GET(0), REG_GET(1)));  \
        return iss_insn_next(iss, insn, pc);                                                                         \
    }                                                                                              \
                                                                                                   \
    static inline iss_reg_t pv_##insn_name##_sci_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)              \
    {                                                                                              \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_int8_t_to_int32_t, REG_GET(0), SIM_GET(0)));  \
        return iss_insn_next(iss, insn, pc);                                                                         \
    }

#define PV_OP_RU_EXEC(insn_name, lib_name)                                                           \
    static inline iss_reg_t pv_##insn_name##_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                    \
    {                                                                                                \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_uint16_t_to_uint32_t, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                           \
    }                                                                                                \
                                                                                                     \
    static inline iss_reg_t pv_##insn_name##_sc_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                 \
    {                                                                                                \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_uint16_t_to_uint32_t, REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                           \
    }                                                                                                \
                                                                                                     \
    static inline iss_reg_t pv_##insn_name##_sci_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                \
    {                                                                                                \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_uint16_t_to_uint32_t, REG_GET(0), UIM_GET(0))); \
        return iss_insn_next(iss, insn, pc);                                                                           \
    }                                                                                                \
                                                                                                     \
    static inline iss_reg_t pv_##insn_name##_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                    \
    {                                                                                                \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_uint8_t_to_uint32_t, REG_GET(0), REG_GET(1)));     \
        return iss_insn_next(iss, insn, pc);                                                                           \
    }                                                                                                \
                                                                                                     \
    static inline iss_reg_t pv_##insn_name##_sc_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                 \
    {                                                                                                \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_uint8_t_to_uint32_t, REG_GET(0), REG_GET(1)));  \
        return iss_insn_next(iss, insn, pc);                                                                           \
    }                                                                                                \
                                                                                                     \
    static inline iss_reg_t pv_##insn_name##_sci_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                \
    {                                                                                                \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_uint8_t_to_uint32_t, REG_GET(0), UIM_GET(0)));  \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        return iss_insn_next(iss, insn, pc);                                                                           \
    }

#define PV_OP_RS_EXEC2(insn_name, lib_name)                                           \
    static inline iss_reg_t pv_##insn_name##_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)     \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_16, REG_GET(0), REG_GET(1)));       \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_h_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)  \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_16, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_h_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_16, REG_GET(0), SIM_GET(0)));    \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)     \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_8, REG_GET(0), REG_GET(1)));        \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_b_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)  \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_8, REG_GET(0), REG_GET(1)));     \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_b_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_8, REG_GET(0), SIM_GET(0)));     \
        return iss_insn_next(iss, insn, pc);                                                            \
    }

#define PV_OP_RU_EXEC2(insn_name, lib_name)                                           \
    static inline iss_reg_t pv_##insn_name##_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)     \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_16, REG_GET(0), REG_GET(1)));       \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_h_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)  \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_16, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_h_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_16, REG_GET(0), UIM_GET(0)));    \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)     \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_8, REG_GET(0), REG_GET(1)));        \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_b_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)  \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_8, REG_GET(0), REG_GET(1)));     \
        return iss_insn_next(iss, insn, pc);                                                            \
    }                                                                                 \
                                                                                      \
    static inline iss_reg_t pv_##insn_name##_b_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
    {                                                                                 \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_8, REG_GET(0), UIM_GET(0)));     \
        return iss_insn_next(iss, insn, pc);                                                            \
    }

#define PV_OP_RRS_EXEC2(insn_name, lib_name)                                                   \
    static inline iss_reg_t pv_##insn_name##_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)              \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_16, REG_GET(2), REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_h_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)           \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_16, REG_GET(2), REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_h_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)          \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_16, REG_GET(0), REG_GET(1), SIM_GET(0))); \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)              \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_8, REG_GET(2), REG_GET(0), REG_GET(1)));     \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_b_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)           \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_8, REG_GET(2), REG_GET(0), REG_GET(1)));  \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_b_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)          \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_8, REG_GET(0), REG_GET(1), SIM_GET(0)));  \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }

#define PV_OP_RRU_EXEC2(insn_name, lib_name)                                                   \
    static inline iss_reg_t pv_##insn_name##_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)              \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_16, REG_GET(2), REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_h_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)           \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_16, REG_GET(2), REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_h_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)          \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_16, REG_GET(0), REG_GET(1), UIM_GET(0))); \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)              \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_8, REG_GET(2), REG_GET(0), REG_GET(1)));     \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_b_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)           \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_8, REG_GET(2), REG_GET(0), REG_GET(1)));  \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }                                                                                          \
                                                                                               \
    static inline iss_reg_t pv_##insn_name##_b_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)          \
    {                                                                                          \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));                                        \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_8, REG_GET(0), REG_GET(1), UIM_GET(0)));  \
        return iss_insn_next(iss, insn, pc);                                                                     \
    }

#define PV_OP1_RS_EXEC(insn_name, lib_name)                                         \
    static inline iss_reg_t pv_##insn_name##_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)   \
    {                                                                               \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        REG_SET(0, LIB_CALL1(lib_VEC_##lib_name##_int16_t_to_int32_t, REG_GET(0))); \
        return iss_insn_next(iss, insn, pc);                                                          \
    }                                                                               \
                                                                                    \
    static inline iss_reg_t pv_##insn_name##_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)   \
    {                                                                               \
        iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));                                        \
        REG_SET(0, LIB_CALL1(lib_VEC_##lib_name##_int8_t_to_int32_t, REG_GET(0)));  \
        return iss_insn_next(iss, insn, pc);                                                          \
    }

PV_OP_RS_EXEC(add, ADD)

PV_OP_RS_EXEC(sub, SUB)

PV_OP_RS_EXEC(avg, AVG)

PV_OP_RU_EXEC(avgu, AVGU)

PV_OP_RS_EXEC(min, MIN)

PV_OP_RU_EXEC(minu, MINU)

PV_OP_RS_EXEC(max, MAX)

PV_OP_RU_EXEC(maxu, MAXU)

PV_OP_RU_EXEC(srl, SRL)

PV_OP_RS_EXEC(sra, SRA)

PV_OP_RU_EXEC(sll, SLL)

PV_OP_RS_EXEC(or, OR)

PV_OP_RS_EXEC(xor, XOR)

PV_OP_RS_EXEC(and, AND)

PV_OP1_RS_EXEC(abs, ABS)

static inline iss_reg_t pv_extract_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_EXT_16, REG_GET(0), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_extract_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_EXT_8, REG_GET(0), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_extractu_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_EXTU_16, REG_GET(0), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_extractu_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_EXTU_8, REG_GET(0), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_insert_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_VEC_INS_16, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_insert_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_VEC_INS_8, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

PV_OP_RS_EXEC2(dotsp, DOTSP)

PV_OP_RU_EXEC2(dotup, DOTUP)

PV_OP_RS_EXEC2(dotusp, DOTUSP)

PV_OP_RRS_EXEC2(sdotsp, SDOTSP)

PV_OP_RRU_EXEC2(sdotup, SDOTUP)

PV_OP_RRS_EXEC2(sdotusp, SDOTUSP)

static inline iss_reg_t pv_shuffle_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_SHUFFLE_16, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_shuffle_h_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_SHUFFLE_SCI_16, REG_GET(0), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_shuffle_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_SHUFFLE_8, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_shufflei0_b_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_SHUFFLEI0_SCI_8, REG_GET(0), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_shufflei1_b_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_SHUFFLEI1_SCI_8, REG_GET(0), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_shufflei2_b_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_SHUFFLEI2_SCI_8, REG_GET(0), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_shufflei3_b_sci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL2(lib_VEC_SHUFFLEI3_SCI_8, REG_GET(0), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_shuffle2_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_VEC_SHUFFLE2_16, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_shuffle2_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_VEC_SHUFFLE2_8, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_pack_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_PACK_SC_16, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_packhi_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_VEC_PACKHI_SC_8, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t pv_packlo_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_VEC_PACKLO_SC_8, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_pack_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_PACK_SC_16, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_pack_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_PACK_SC_H_16, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_packhi_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_VEC_PACKHI_SC_8, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_packlo_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_VEC_PACKLO_SC_8, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_add_div2_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_ADD_int16_t_to_int32_t_div2, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_add_div4_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_ADD_int16_t_to_int32_t_div4, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_add_div8_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_ADD_int16_t_to_int32_t_div8, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_sub_div2_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_SUB_int16_t_to_int32_t_div2, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_sub_div4_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_SUB_int16_t_to_int32_t_div4, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_sub_div8_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_SUB_int16_t_to_int32_t_div8, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_subrotmj_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_ADD_16_ROTMJ, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_subrotmj_div2_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_ADD_16_ROTMJ_DIV2, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_subrotmj_div4_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_ADD_16_ROTMJ_DIV4, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_subrotmj_div8_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_VEC_ADD_16_ROTMJ_DIV8, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_cplxconj_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    REG_SET(0, LIB_CALL1(lib_CPLX_CONJ_16, REG_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_cplxmul_r_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_CPLXMUL_H_R, REG_GET(0), REG_GET(1), REG_GET(2), 0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_cplxmul_r_div2_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_CPLXMUL_H_R, REG_GET(0), REG_GET(1), REG_GET(2), 1));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_cplxmul_r_div4_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_CPLXMUL_H_R, REG_GET(0), REG_GET(1), REG_GET(2), 2));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_cplxmul_r_div8_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_CPLXMUL_H_R, REG_GET(0), REG_GET(1), REG_GET(2), 3));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_cplxmul_i_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_CPLXMUL_H_I, REG_GET(0), REG_GET(1), REG_GET(2), 0));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_cplxmul_i_div2_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_CPLXMUL_H_I, REG_GET(0), REG_GET(1), REG_GET(2), 1));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_cplxmul_i_div4_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_CPLXMUL_H_I, REG_GET(0), REG_GET(1), REG_GET(2), 2));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t cv_cplxmul_i_div8_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_CPLXMUL_H_I, REG_GET(0), REG_GET(1), REG_GET(2), 3));
    return iss_insn_next(iss, insn, pc);
}

PV_OP_RS_EXEC(cmpeq, CMPEQ)

PV_OP_RS_EXEC(cmpne, CMPNE)

PV_OP_RS_EXEC(cmpgt, CMPGT)

PV_OP_RS_EXEC(cmpge, CMPGE)

PV_OP_RS_EXEC(cmplt, CMPLT)

PV_OP_RS_EXEC(cmple, CMPLE)

PV_OP_RU_EXEC(cmpgtu, CMPGTU)

PV_OP_RU_EXEC(cmpgeu, CMPGEU)

PV_OP_RU_EXEC(cmpltu, CMPLTU)

PV_OP_RU_EXEC(cmpleu, CMPLEU)

static inline iss_reg_t p_bneimm_exec_common(Iss *iss, iss_insn_t *insn, iss_reg_t pc, int perf)
{
    iss->regfile.memcheck_branch_reg(REG_IN(0));

    if ((int32_t)REG_GET(0) != SIM_GET(1))
    {
        iss->timing.event_taken_branch_account();
        return pc + SIM_GET(0);
    }
    else
    {
        if (perf)
        {
            iss->timing.event_branch_account();
        }
        return iss_insn_next(iss, insn, pc);
    }
}

static inline iss_reg_t p_bneimm_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    return p_bneimm_exec_common(iss, insn, pc, 0);
}

static inline iss_reg_t p_bneimm_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    return p_bneimm_exec_common(iss, insn, pc, 1);
}

static inline iss_reg_t p_beqimm_exec_common(Iss *iss, iss_insn_t *insn, iss_reg_t pc, int perf)
{
    iss->regfile.memcheck_branch_reg(REG_IN(0));

    if ((int32_t)REG_GET(0) == SIM_GET(1))
    {
        iss->timing.event_taken_branch_account();
        return pc + SIM_GET(0);
    }
    else
    {
        if (perf)
        {
            iss->timing.event_branch_account();
        }
        return iss_insn_next(iss, insn, pc);
    }
}

static inline iss_reg_t p_beqimm_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    return p_beqimm_exec_common(iss, insn, pc, 0);
}

static inline iss_reg_t p_beqimm_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    return p_beqimm_exec_common(iss, insn, pc, 1);
}

static inline iss_reg_t p_mac_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_MAC, REG_GET(2), REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_msu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_MSU, REG_GET(2), REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mul_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_MULS, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_muls_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_MUL_SL_SL, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mulhhs_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_MUL_SH_SH, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mulsN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_MUL_SL_SL_NR, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mulhhsN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_MUL_SH_SH_NR, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mulsNR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_MUL_SL_SL_NR_R, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mulhhsNR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_MUL_SH_SH_NR_R, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mulu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_MUL_ZL_ZL, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mulhhu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL2(lib_MUL_ZH_ZH, REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_muluN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_MUL_ZL_ZL_NR, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mulhhuN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_MUL_ZH_ZH_NR, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_muluNR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_MUL_ZL_ZL_NR_R, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_mulhhuNR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_MUL_ZH_ZH_NR_R, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_macs_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_MAC_SL_SL, REG_GET(2), REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_machhs_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_MAC_SH_SH, REG_GET(2), REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_macsN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_MAC_SL_SL_NR, REG_GET(2), REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_machhsN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_MAC_SH_SH_NR, REG_GET(2), REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_macsNR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_MAC_SL_SL_NR_R_cv32e40p, REG_GET(2), REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_machhsNR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_MAC_SH_SH_NR_R_cv32e40p, REG_GET(2), REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_macu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_MAC_ZL_ZL, REG_GET(2), REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_machhu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_MAC_ZH_ZH, REG_GET(2), REG_GET(0), REG_GET(1)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_macuN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_MAC_ZL_ZL_NR, REG_GET(2), REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_machhuN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_MAC_ZH_ZH_NR, REG_GET(2), REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_macuNR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_MAC_ZL_ZL_NR_R_cv32e40p, REG_GET(2), REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_machhuNR_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL4(lib_MAC_ZH_ZH_NR_R_cv32e40p, REG_GET(2), REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_addNi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_ADD_NR, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_adduNi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_ADD_NRU, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_addRNi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_ADD_NR_R, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_adduRNi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_ADD_NR_RU, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_subNi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_SUB_NR, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_subuNi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_SUB_NRU, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_subRNi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_SUB_NR_R, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_subuRNi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    REG_SET(0, LIB_CALL3(lib_SUB_NR_RU, REG_GET(0), REG_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_addN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_ADD_NR, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_adduN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_ADD_NRU, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_addRN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_ADD_NR_R, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_adduRN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_ADD_NR_RU, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_subN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_SUB_NR, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_subuN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_SUB_NRU, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_subRN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_SUB_NR_R, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_subuRN_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(2));
    REG_SET(0, LIB_CALL3(lib_SUB_NR_RU, REG_GET(0), REG_GET(1), REG_GET(2)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_clipi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    int low = (int)-(1 << MAX((int)UIM_GET(0) - 1, 0));
    int high = (1 << MAX((int)UIM_GET(0) - 1, 0)) - 1;
    REG_SET(0, LIB_CALL3(lib_CLIP, REG_GET(0), low, high));

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_clipui_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    int high = (1 << MAX((int)UIM_GET(0) - 1, 0)) - 1;
    REG_SET(0, LIB_CALL2(lib_CLIPU, REG_GET(0), high));

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_clip_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    int low = -REG_GET(1) - 1;
    int high = REG_GET(1);
    REG_SET(0, LIB_CALL3(lib_CLIP, REG_GET(0), low, high));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_clipu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    int high = REG_GET(1);
    REG_SET(0, LIB_CALL2(lib_CLIPU, REG_GET(0), high));

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_bclri_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL2(lib_BCLR, REG_GET(0), ((1ULL << width) - 1) << shift));

    iss->regfile.memcheck_set(REG_OUT(0),
        iss->regfile.memcheck_get(REG_IN(0)) | ((1ULL << width) - 1) << shift);

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_bclr_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    int width = ((REG_GET(1) >> 5) & 0x1f) + 1;
    int shift = REG_GET(1) & 0x1f;
    REG_SET(0, LIB_CALL2(lib_BCLR, REG_GET(0), ((1ULL << width) - 1) << shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_extracti_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL3(lib_BEXTRACT, REG_GET(0), width, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_extractui_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL3(lib_BEXTRACTU, REG_GET(0), ((1ULL << width) - 1) << shift, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_extract_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    int width = ((REG_GET(1) >> 5) & 0x1f) + 1;
    int shift = REG_GET(1) & 0x1f;
    REG_SET(0, LIB_CALL3(lib_BEXTRACT, REG_GET(0), width, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_extractu_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    int width = ((REG_GET(1) >> 5) & 0x1f) + 1;
    int shift = REG_GET(1) & 0x1f;
    REG_SET(0, LIB_CALL3(lib_BEXTRACTU, REG_GET(0), ((1ULL << width) - 1) << shift, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_inserti_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL4(lib_BINSERT, REG_GET(0), REG_GET(1), ((1ULL << width) - 1) << shift, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_insert_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    int width = ((REG_GET(2) >> 5) & 0x1F) + 1;
    int shift = REG_GET(2) & 0x1F;
    REG_SET(0, LIB_CALL4(lib_BINSERT, REG_GET(0), REG_GET(1), ((1ULL << width) - 1) << shift, shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_bseti_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    int width = UIM_GET(0) + 1;
    int shift = UIM_GET(1);
    REG_SET(0, LIB_CALL2(lib_BSET, REG_GET(0), ((1ULL << (width)) - 1) << shift));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t p_bset_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(0));
    iss->regfile.memcheck_merge(REG_OUT(0), REG_IN(1));
    int width = ((REG_GET(1) >> 5) & 0x1f) + 1;
    int shift = REG_GET(1) & 0x1f;
    REG_SET(0, LIB_CALL2(lib_BSET, REG_GET(0), ((1ULL << (width)) - 1) << shift));
    return iss_insn_next(iss, insn, pc);
}


#endif
