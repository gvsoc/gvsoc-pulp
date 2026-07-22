// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

/* CV32E40P privileged-instruction handlers, replacing
 * <cpu/iss/include/isa/priv.hpp> for the whole priv subset (the recipe
 * swaps the subset include, so this file provides the full surface:
 * csrr*, wfi, xret, sfence.vma).
 *
 * Difference from the generic handlers: CSRRC with rs1=x0 and
 * CSRRSI/CSRRCI with uimm=0 must not write the CSR (privileged spec
 * §2.2), so a read of a read-only CSR through them is legal. The generic
 * csrrc/csrrsi/csrrci treat every access as a write. Same fix as the v1
 * core header (cpu/iss/include/cores/cv32e40p/priv.hpp).
 */

#pragma once

static inline void csr_decode(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // In case traces are active, convert the CSR number into a name
#ifdef VP_TRACE_ACTIVE
    insn->args[2].flags = (iss_decoder_arg_flag_e)(insn->args[2].flags | ISS_DECODER_ARG_FLAG_DUMP_NAME);
    insn->args[2].name = iss_csr_name(iss, UIM_GET(0));
#endif
}

static inline iss_reg_t csrrw_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    CsrAbtractReg *csr = iss->csr.get_csr(UIM_GET(0));
    if (csr)
    {
        return csr->handle(iss, insn, pc, REG_GET(0));
    }

    iss_reg_t value;
    iss_reg_t reg_value = REG_GET(0);

    if (iss_csr_read(iss, insn, UIM_GET(0), &value) == 0)
    {
        if (insn->out_regs[0] != 0)
        {
            iss->regfile.memcheck_set_valid(REG_OUT(0), true);
            REG_SET(0, value);
        }
    }

    iss_csr_write(iss, insn, UIM_GET(0), reg_value);

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t csrrc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t value;
    iss_reg_t reg_value = REG_GET(0);

    CsrAbtractReg *csr = iss->csr.get_csr(UIM_GET(0));
    if (csr && !csr->check_access(iss, REG_IN(0) != 0, true))
    {
        return pc;
    }

    if (iss_csr_read(iss, insn, UIM_GET(0), &value) == 0)
    {
        if (insn->out_regs[0] != 0)
        {
            iss->regfile.memcheck_set_valid(REG_OUT(0), true);
            REG_SET(0, value);
        }
    }

    if (REG_IN(0) != 0)
    {
        iss_csr_write(iss, insn, UIM_GET(0), value & ~reg_value);
    }

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t csrrs_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t value;
    iss_reg_t reg_value = REG_GET(0);

    CsrAbtractReg *csr = iss->csr.get_csr(UIM_GET(0));
    if (csr && !csr->check_access(iss, REG_IN(0) != 0, true))
    {
        return pc;
    }

    if (iss_csr_read(iss, insn, UIM_GET(0), &value) == 0)
    {
        if (insn->out_regs[0] != 0)
        {
            iss->regfile.memcheck_set_valid(REG_OUT(0), true);
            REG_SET(0, value);
        }
    }

    if (REG_IN(0) != 0)
    {
        iss_csr_write(iss, insn, UIM_GET(0), value | reg_value);
    }

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t csrrwi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    CsrAbtractReg *csr = iss->csr.get_csr(UIM_GET(0));
    if (csr)
    {
        return csr->handle(iss, insn, pc, UIM_GET(1));
    }

    iss_reg_t value;

    if (iss_csr_read(iss, insn, UIM_GET(0), &value) == 0)
    {
        if (insn->out_regs[0] != 0)
        {
            iss->regfile.memcheck_set_valid(REG_OUT(0), true);
            REG_SET(0, value);
        }
    }

    iss_csr_write(iss, insn, UIM_GET(0), UIM_GET(1));

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t csrrci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t value;

    CsrAbtractReg *csr = iss->csr.get_csr(UIM_GET(0));
    if (csr && !csr->check_access(iss, UIM_GET(1) != 0, true))
    {
        return pc;
    }

    if (iss_csr_read(iss, insn, UIM_GET(0), &value) == 0)
    {
        if (insn->out_regs[0] != 0)
        {
            iss->regfile.memcheck_set_valid(REG_OUT(0), true);
            REG_SET(0, value);
        }
    }

    if (UIM_GET(1) != 0)
    {
        iss_csr_write(iss, insn, UIM_GET(0), value & ~UIM_GET(1));
    }

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t csrrsi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t value;

    CsrAbtractReg *csr = iss->csr.get_csr(UIM_GET(0));
    if (csr && !csr->check_access(iss, UIM_GET(1) != 0, true))
    {
        return pc;
    }

    if (iss_csr_read(iss, insn, UIM_GET(0), &value) == 0)
    {
        if (insn->out_regs[0] != 0)
        {
            iss->regfile.memcheck_set_valid(REG_OUT(0), true);
            REG_SET(0, value);
        }
    }

    if (UIM_GET(1) != 0)
    {
        iss_csr_write(iss, insn, UIM_GET(0), value | UIM_GET(1));
    }

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t wfi_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->irq.wfi_handle(insn);
    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t mret_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->exec.irq_exit.set(1);
    iss->timing.stall_insn_dependency_account(5);
    return iss->core.mret_handle();
}

static inline iss_reg_t dret_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    /* dret is legal only in debug mode; outside it the RTL raises an illegal
     * instruction (RISC-V Debug spec, cv32e40p debug.rst). dret_handle()
     * itself is unconditional (clears debug_mode, restores irq_enable, jumps
     * to depc), so the guard must live here. */
    if (!iss->exec.debug_mode)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        return pc;
    }
    return iss->core.dret_handle();
}

static inline iss_reg_t sret_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->timing.stall_insn_dependency_account(5);
    return iss->core.sret_handle();
}

static inline iss_reg_t sfence_vma_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->core.mode_get() == PRIV_S && iss->csr.mstatus.tvm)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        return pc;
    }
    else
    {
#ifdef CONFIG_GVSOC_ISS_MMU
        iss->mmu.flush(REG_GET(0), REG_GET(1));
#endif
        iss->insn_cache.mode_flush();
        return iss_insn_next(iss, insn, pc);
    }
}
