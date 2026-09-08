// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#include <cpu/iss_v2/include/iss.hpp>

Cv32e40pException::Cv32e40pException(Iss &iss)
: Exception(iss), iss(iss)
{
    /* dm_exception_addr_i of the RTL: exceptions taken while in debug
     * mode enter here (RISC-V debug spec 0.13.2). Falls back to
     * the debug handler entry when the platform config lacks the key. */
    js::Config *conf = iss.get_js_config()->get("debug_exception_handler");
    this->debug_exception_handler_addr =
        conf != NULL ? (iss_addr_t)conf->get_int() : this->debug_handler_addr;
}

void Cv32e40pException::raise(iss_reg_t pc, int id)
{
    /* Commit-stream consumers gate state compares on this (events.hpp). */
    this->iss.timing.trap_seq++;

    /* Exception taken while in debug mode (RISC-V debug spec 0.13.2,
     * CV32E40P manual): the hart jumps to dm_exception_addr and stays in
     * debug mode; mepc/mcause/mstatus and the privilege mode are NOT
     * updated. The generic raise would route to mtvec and clobber them. */
    if (id != ISS_EXCEPT_DEBUG && this->iss.exec.debug_mode)
    {
        this->iss.exec.switch_to_full_mode();
        this->iss.exec.has_exception = true;
        this->iss.exec.exception_pc =
            this->debug_exception_handler_addr & ~(iss_reg_t)0x3;
        return;
    }

    this->Exception::raise(pc, id);

    /* Exceptions enter at the mtvec base. mtvec.value keeps the RTL mode
     * bits (mtvec[1:0] = 01) and the generic raise copies it verbatim
     * into the entry PC. Assumes the mtvec-based entry path, i.e.
     * CONFIG_GVSOC_ISS_RISCV_EXCEPTIONS (always set by Cv32e40pIrq). */
    if (id != ISS_EXCEPT_DEBUG)
    {
        this->iss.exec.exception_pc &= ~(iss_reg_t)0x3;
    }
}
