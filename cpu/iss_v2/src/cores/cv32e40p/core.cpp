// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#include <cpu/iss_v2/include/iss.hpp>

iss_reg_t Cv32e40pCore::mret_handle()
{
    /* mcause holds its value across MRET on CV32E40P (cleared only by the
     * next trap or an explicit CSR write); the generic handler zeroes it. */
    iss_reg_t mcause = this->iss.csr.mcause.value;
    iss_reg_t pc = this->Core::mret_handle();
    this->iss.csr.mcause.value = mcause;
    return pc;
}
