// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

// Snitch inline code needing the complete Iss, and the Snitch specific
// instruction handlers. Included after iss.hpp, before the ISA handlers.

#pragma once

#include <cpu/iss_v2/include/iss.hpp>
#include <cpu/iss_v2/include/utils.hpp>
#include "cpu/iss/include/isa_lib/int.h"
#include "cpu/iss_v2/include/isa_lib/macros.h"

// Alternate FP16 / FP8 formats, selected by the fmode CSR
static inline iss_reg_t fmode_get(Iss *iss, iss_insn_t *insn)
{
    return iss->arch.fmode.value;
}

#include <cpu/iss/include/isa/rvXf16.hpp>
#include <cpu/iss/include/isa/rvXf16alt.hpp>
#include <cpu/iss/include/isa/rvXf8.hpp>
#include <cpu/iss/include/isa/rvXf8alt.hpp>
#include <cpu/iss/include/isa/rv32Xfvec.hpp>
#include <cpu/iss/include/isa/rvXf16_switch.hpp>
#include <cpu/iss/include/isa/rvXf8_switch.hpp>

inline uint64_t SnitchV2Regfile::get_freg(int reg)
{
    int lid = reg - ISS_NB_REGS;
    if (unlikely(this->iss.arch.ssr.is_enabled()) && lid < CONFIG_GVSOC_ISS_SNITCH_NB_SSRS)
    {
        return this->iss.arch.ssr.streamers[lid]->pop(this->iss.arch.fp_ss.issue_id);
    }
    return Regfile::get_freg(reg);
}

inline void SnitchV2Regfile::set_freg(int reg, uint64_t value)
{
    int lid = reg - ISS_NB_REGS;
    if (unlikely(this->iss.arch.ssr.is_enabled()) && lid < CONFIG_GVSOC_ISS_SNITCH_NB_SSRS)
    {
        this->iss.arch.ssr.streamers[lid]->push(value, this->iss.arch.fp_ss.result_cycle);
        return;
    }
    Regfile::set_freg(reg, value);
}


// FP loads and stores. They are executed by the FP subsystem, with the
// address captured when the integer core offloaded them.

#define SNITCH_FP_LOAD(name, size)                                                      \
static inline iss_reg_t name##_snitchv2_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)  \
{                                                                                       \
    iss->arch.fp_ss.load(insn, iss->arch.fp_ss.lsu_addr(), size, REG_OUT(0));           \
    return iss_insn_next(iss, insn, pc);                                                \
}                                                                                       \
static inline iss_reg_t name##_snitchv2_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
{                                                                                       \
    return name##_snitchv2_exec(iss, insn, pc);                                         \
}

#define SNITCH_FP_STORE(name, size)                                                     \
static inline iss_reg_t name##_snitchv2_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)  \
{                                                                                       \
    iss->arch.fp_ss.store(insn, iss->arch.fp_ss.lsu_addr(), size, REG_IN(1));           \
    return iss_insn_next(iss, insn, pc);                                                \
}                                                                                       \
static inline iss_reg_t name##_snitchv2_exec_fast(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
{                                                                                       \
    return name##_snitchv2_exec(iss, insn, pc);                                         \
}

SNITCH_FP_LOAD(flb, 1)
SNITCH_FP_LOAD(flh, 2)
SNITCH_FP_LOAD(flw, 4)
SNITCH_FP_LOAD(fld, 8)
SNITCH_FP_STORE(fsb, 1)
SNITCH_FP_STORE(fsh, 2)
SNITCH_FP_STORE(fsw, 4)
SNITCH_FP_STORE(fsd, 8)

// FREP is handled by the FP subsystem sequencer, its handler is never called
static inline iss_reg_t frep_o_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t frep_i_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    return iss_insn_next(iss, insn, pc);
}

// SSR configuration accesses, executed by the integer core. SCFGRI / SCFGWI
// take the register and the SSR from the immediate, SCFGR / SCFGW from rs2
// (bits 4:0 the SSR, 11:5 the register).

static inline iss_reg_t scfgri_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    REG_SET(0, iss->arch.ssr.cfg_read(UIM_GET(1), UIM_GET(0)));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t scfgwi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (!iss->arch.ssr.cfg_write(UIM_GET(1), UIM_GET(0), REG_GET(0)))
    {
        iss->exec.insn_stall();
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t scfgr_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t addr = REG_GET(0);
    REG_SET(0, iss->arch.ssr.cfg_read((addr >> 5) & 0x7f, addr & 0x1f));
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t scfgw_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t addr = REG_GET(1);
    if (!iss->arch.ssr.cfg_write((addr >> 5) & 0x7f, addr & 0x1f, REG_GET(0)))
    {
        iss->exec.insn_stall();
        return pc;
    }
    return iss_insn_next(iss, insn, pc);
}

