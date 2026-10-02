// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

/* CV32E40P handlers of the priv subset, in place of
 * <cpu/iss/include/isa/priv.hpp>. Unlike the generic handlers, an address
 * outside the CSR map raises illegal-instruction, and CSRRC with rs1 = x0
 * and CSRRSI/CSRRCI with uimm = 0 do not write the CSR (privileged spec
 * 2.2), so they can read a read-only CSR. */

#pragma once

static inline void csr_decode(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // In case traces are active, convert the CSR number into a name
#ifdef VP_TRACE_ACTIVE
    insn->args[2].flags =
        (iss_decoder_arg_flag_e)(insn->args[2].flags | ISS_DECODER_ARG_FLAG_DUMP_NAME);
    insn->args[2].name = iss_csr_name(iss, UIM_GET(0));
#endif
}

static inline iss_reg_t csrrw_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    CsrAbtractReg *csr = iss->csr.get_csr(UIM_GET(0));
    if (csr == NULL)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        return pc;
    }
    return csr->handle(iss, insn, pc, REG_GET(0));
}

static inline iss_reg_t csrrc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t value;
    iss_reg_t reg_value = REG_GET(0);

    CsrAbtractReg *csr = iss->csr.get_csr(UIM_GET(0));
    if (csr == NULL)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        return pc;
    }
    if (!csr->check_access(iss, REG_IN(0) != 0, true))
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
    if (csr == NULL)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        return pc;
    }
    if (!csr->check_access(iss, REG_IN(0) != 0, true))
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
    if (csr == NULL)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        return pc;
    }
    return csr->handle(iss, insn, pc, UIM_GET(1));
}

static inline iss_reg_t csrrci_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t value;

    CsrAbtractReg *csr = iss->csr.get_csr(UIM_GET(0));
    if (csr == NULL)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        return pc;
    }
    if (!csr->check_access(iss, UIM_GET(1) != 0, true))
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
    if (csr == NULL)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        return pc;
    }
    if (!csr->check_access(iss, UIM_GET(1) != 0, true))
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
    /* wfi is a nop where the RTL does not sleep: in debug mode, while
     * single-stepping, with a pending debug request, and with COREV_CLUSTER,
     * where only cv.elw sleeps (cv32e40p_controller.sv, debug_wfi_no_sleep_o). */
    if (!CONFIG_GVSOC_ISS_CV32E40P_COREV_CLUSTER &&
        !iss->irq.debug_request_pending() && !iss->exec.debug_mode &&
        !((iss->csr.dcsr >> 2) & 1))
    {
        iss->irq.wfi_handle(insn);
    }
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
    // dret is illegal outside debug mode (user manual, debug.rst).
    if (!iss->exec.debug_mode)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        return pc;
    }
    // With dcsr.step=1, opens the single-step window.
    iss->irq.dret_step_check();
    return iss->core.dret_handle();
}

static inline iss_reg_t sret_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // There is no S-mode, so the RTL decodes sret as illegal.
    iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
    return pc;
}

static inline iss_reg_t sfence_vma_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // There is no S-mode and no address translation, so sfence.vma is illegal.
    iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
    return pc;
}
