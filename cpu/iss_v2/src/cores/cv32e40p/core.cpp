// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#include <cpu/iss_v2/include/iss.hpp>

iss_reg_t Cv32e40pCore::mret_handle()
{
    /* MRET executed while in debug mode (CV32E40P UM, debug.rst): the PC
     * jumps to dm_exception_addr "without affecting status registers" -
     * no mstatus/mcause/privilege side effects, the hart stays in debug
     * mode. The generic handler below would return to mepc and restore
     * mstatus.mie. */
    if (this->iss.exec.debug_mode)
    {
        this->iss.exec.switch_to_full_mode();
        return this->iss.exception.debug_exception_handler_addr & ~(iss_reg_t)0x3;
    }

    /* mcause holds its value across MRET on CV32E40P (cleared only by the
     * next trap or an explicit CSR write); the generic handler zeroes it. */
    iss_reg_t mcause = this->iss.csr.mcause.value;
    iss_reg_t pc = this->Core::mret_handle();
    this->iss.csr.mcause.value = mcause;
    return pc;
}
