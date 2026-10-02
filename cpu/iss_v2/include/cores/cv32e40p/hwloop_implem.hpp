// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <cpu/iss_v2/include/cores/cv32e40p/hwloop.hpp>
#include <cpu/iss_v2/include/hwloop/hwloop_implem.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>

inline iss_reg_t Cv32e40pHwloop::check(iss_reg_t pc, iss_reg_t next_pc)
{
    if (this->iss.exec.has_exception || this->iss.irq.ebreak_to_debug)
    {
        this->iss.irq.ebreak_to_debug = false;
        return next_pc;
    }
    if (!this->iss.exec.cosim->enabled())
    {
        return this->Hwloop::check(pc, next_pc);
    }

    // The loop counter decremented at the loop end is part of the record.
    iss_reg_t count[CONFIG_GVSOC_ISS_NB_HWLOOP];
    for (int i = 0; i < CONFIG_GVSOC_ISS_NB_HWLOOP; i++)
    {
        count[i] = this->get_count(i);
    }
    next_pc = this->Hwloop::check(pc, next_pc);
    for (int i = 0; i < CONFIG_GVSOC_ISS_NB_HWLOOP; i++)
    {
        if (this->get_count(i) != count[i])
        {
            this->iss.exec.cosim->csr(0xCC2 + i * 4, CV32E40P_COSIM_CSR_RETIRE);
        }
    }
    return next_pc;
}
