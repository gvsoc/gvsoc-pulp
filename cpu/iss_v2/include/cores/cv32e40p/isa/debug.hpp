// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

/* ebreak and c.ebreak of the CV32E40P (cv32e40p_controller.sv, user manual
 * debug.rst). In debug mode they enter the debug handler again. Outside it,
 * they enter debug mode with cause 1 when dcsr.ebreakm=1
 * (Cv32e40pIrq::ebreak_debug_entry), and raise a breakpoint exception
 * otherwise. An ebreak that enters the debug handler does not retire, and its
 * record is trapped, as RVFI reports it. The RTL has no semihosting. */

#pragma once

static inline iss_reg_t cv32e40p_ebreak_common(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (iss->exec.debug_mode)
    {
        if (iss->exec.cosim->enabled())
        {
            iss->exec.cosim->trap(ISS_EXCEPT_BREAKPOINT, iss->irq.debug_handler, true);
        }
        iss->irq.ebreak_to_debug = true;
        return iss->irq.debug_handler;
    }

    if (iss->csr.ebreak_m_mode_enters_debug())
    {
        iss->irq.ebreak_to_debug = true;
        return iss->irq.ebreak_debug_entry(pc);
    }

    iss->exception.raise(pc, ISS_EXCEPT_BREAKPOINT);
    return pc;
}

static inline iss_reg_t cv32e40p_ebreak_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    return cv32e40p_ebreak_common(iss, insn, pc);
}

static inline iss_reg_t cv32e40p_c_ebreak_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss->timing.event_rvc_account(1);
    return cv32e40p_ebreak_common(iss, insn, pc);
}
