// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <cpu/iss_v2/include/cores/cv32e40p/regfile.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>

inline void Cv32e40pRegfile::set_reg(int reg, uint64_t value)
{
    if (reg == 0)
    {
        return;
    }
    this->Regfile::set_reg(reg, value);
    if (this->iss.exec.cosim->enabled())
    {
        // FP loads complete through here with the unified index.
        if (this->is_freg(reg))
        {
            this->iss.exec.cosim->fpr(this->get_reg_lid(reg), (uint32_t)value);
        }
        else
        {
            this->iss.exec.cosim->gpr(reg, (uint32_t)value);
        }
    }
}

inline void Cv32e40pRegfile::set_freg(int reg, uint64_t value)
{
    // reg is 32 + rd, or rd with Zfinx, where the F instructions write the
    // integer registers. set_reg records both.
    this->set_reg(reg, value);
}

inline bool Cv32e40pRegfile::scoreboard_insn_check(iss_insn_t *insn)
{
    if (this->Regfile::scoreboard_insn_check(insn))
    {
        return true;
    }
    // A debug entry moved the PC, so fetch again.
    if (this->iss.irq.dispatch_decide(insn))
    {
        return true;
    }
    return this->iss.exec.cosim->dispatch(insn);
}

inline void Cv32e40pRegfile::scoreboard_insn_clear(iss_insn_t *insn)
{
    this->Regfile::scoreboard_insn_clear(insn);
    if (this->iss.exec.cosim->enabled())
    {
        this->iss.exec.cosim->retry(insn);
    }
}
