// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#include <cpu/iss_v2/include/iss.hpp>

void Cv32e40pException::raise(iss_reg_t pc, int id)
{
    /* Commit-stream consumers gate state compares on this (events.hpp). */
    this->iss.timing.trap_seq++;

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
