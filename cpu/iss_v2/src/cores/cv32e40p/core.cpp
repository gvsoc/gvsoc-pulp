// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#include <cpu/iss_v2/include/iss.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>

Cv32e40pCore::Cv32e40pCore(Iss &iss)
: Core(iss), iss(iss)
{
    /* mstatus resets with MPP=M and FS=Off in every configuration. This is
     * set after the Core constructor, which puts FS Dirty in the reset value. */
    this->iss.csr.mstatus.reset_val = 0x00001800;
}

void Cv32e40pCore::reset(bool active)
{
    this->Core::reset(active);
    if (active)
    {
        // The FF register file resets every integer and floating-point
        // register to zero (cv32e40p_register_file_ff.sv), and an interrupt
        // handler that saves the registers stores that value.
        for (int reg = 1; reg < ISS_NB_REGS + ISS_NB_FREGS; ++reg)
            this->iss.regfile.set_reg(reg, 0);
    }
}

iss_reg_t Cv32e40pCore::mret_handle()
{
    /* In debug mode MRET jumps to dm_exception_addr without changing any
     * status register, and the hart stays in debug mode (user manual,
     * debug.rst). */
    if (this->iss.exec.debug_mode)
    {
        this->iss.exec.switch_to_full_mode();
        return this->iss.exception.debug_exception_handler_addr & ~(iss_reg_t)0x3;
    }

    /* mcause keeps its value across MRET until the next trap or CSR write,
     * but the generic handler clears it. */
    iss_reg_t mcause = this->iss.csr.mcause.value;
    iss_reg_t pc = this->Core::mret_handle();
    this->iss.csr.mcause.value = mcause;
    // The generic handler writes the status fields without a CSR access, so
    // report mstatus here.
    if (this->iss.exec.cosim->enabled())
    {
        this->iss.exec.cosim->csr(0x300, CV32E40P_COSIM_CSR_INSN);
    }
    return pc;
}
