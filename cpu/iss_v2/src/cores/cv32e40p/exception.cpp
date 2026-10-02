// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#include <cpu/iss_v2/include/iss.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>

Cv32e40pException::Cv32e40pException(Iss &iss)
: Exception(iss), iss(iss)
{
    /* Entry of the exceptions taken in debug mode (RTL dm_exception_addr_i),
     * set by the core recipe. */
    js::Config *conf = iss.get_js_config()->get("debug_exception_handler");
    this->debug_exception_handler_addr =
        conf != NULL ? (iss_addr_t)conf->get_int() : this->debug_handler_addr;
}

void Cv32e40pException::raise(iss_reg_t pc, int id)
{
    /* An exception in debug mode jumps to dm_exception_addr and leaves mepc,
     * mcause, mstatus and the privilege mode unchanged (user manual,
     * debug.rst). */
    if (id != ISS_EXCEPT_DEBUG && this->iss.exec.debug_mode)
    {
        this->iss.exec.switch_to_full_mode();
        this->iss.exec.has_exception = true;
        this->iss.exec.exception_pc =
            this->debug_exception_handler_addr & ~(iss_reg_t)0x3;
        if (this->iss.exec.cosim->enabled())
        {
            this->iss.exec.cosim->trap(id, this->iss.exec.exception_pc, true);
        }
        return;
    }

    this->Exception::raise(pc, id);

    /* Exceptions enter at the mtvec base, but the generic raise copies the
     * mode bits of mtvec (01) into the entry PC. */
    if (id != ISS_EXCEPT_DEBUG)
    {
        this->iss.exec.exception_pc &= ~(iss_reg_t)0x3;
        if (this->iss.exec.cosim->enabled())
        {
            this->iss.exec.cosim->trap(this->iss.csr.mcause.value, this->iss.exec.exception_pc,
                false);
        }
    }
}
