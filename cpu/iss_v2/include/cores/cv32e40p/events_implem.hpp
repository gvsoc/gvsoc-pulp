// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include "cpu/iss_v2/include/cores/cv32e40p/csr.hpp"
#include <vp/vp.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/events.hpp>
#include <cpu/iss_v2/include/event/event_implem.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>

inline void Cv32e40pEvents::event_load_account(int incr)
{
    Events::event_load_account(incr);
    this->pending_events |= CV32E40P_HPM_LD;
}

inline void Cv32e40pEvents::event_store_account(int incr)
{
    Events::event_store_account(incr);
    this->pending_events |= CV32E40P_HPM_ST;
}

inline void Cv32e40pEvents::event_branch_account()
{
    Events::event_branch_account();
    this->pending_events |= CV32E40P_HPM_BRANCH;
}

inline void Cv32e40pEvents::event_taken_branch_account()
{
    // A taken branch fires both RTL event lines.
    Events::event_taken_branch_account();
    this->pending_events |= CV32E40P_HPM_BRANCH | CV32E40P_HPM_BRANCH_TAKEN;
}

inline void Cv32e40pEvents::event_jump_account()
{
    Events::event_jump_account();
    this->pending_events |= CV32E40P_HPM_JUMP;
}

inline void Cv32e40pEvents::event_jalr_account(int rs1)
{
    // The RTL jump line fires on JALR, c.jr and c.jalr too (cv32e40p_id_stage.sv).
    Events::event_jalr_account(rs1);
    this->pending_events |= CV32E40P_HPM_JUMP;
#if defined(CONFIG_GVSOC_ISS_JUMP_STALL_CYCLES)
    // Same one-cycle penalty as jal; the generic event_jalr_account() has no
    // timing, only event_jump_account() does.
    this->iss.exec.stall_cycles_inc(1);
#endif
}

inline void Cv32e40pEvents::event_retire_account(iss_insn_t *insn)
{
    Events::event_retire_account(insn);

    // A trapping instruction does not retire, so its event lines are dropped.
    if (this->iss.exec.has_exception)
    {
        this->pending_events = 0;
        if (this->iss.exec.cosim->enabled())
        {
            this->iss.exec.cosim->retire(insn);
        }
        return;
    }

    // A compressed opcode carries the next parcel in its upper half.
    iss_reg_t enc = (insn->size == 2) ? (insn->opcode & 0xFFFF) : insn->opcode;

    /* The minstret and compressed lines exclude ebreak
     * (cv32e40p_id_stage.sv). This covers the ebreak that enters debug mode,
     * which does not trap. */
    bool count_instr = !(enc == 0x00100073u
                         || (insn->size == 2 && enc == 0x9002u));
    uint32_t events = this->pending_events
        | (count_instr ? (CV32E40P_HPM_INSTR
            | (insn->size == 2 ? CV32E40P_HPM_COMP_INSTR : 0)) : 0);
    this->pending_events = 0;
    this->iss.csr.hpm_commit(events, count_instr);

    // After hpm_commit, so that the record has the new minstret.
    if (this->iss.exec.cosim->enabled())
    {
        this->iss.exec.cosim->retire(insn);
    }
}

inline void Cv32e40pEvents::insn_stall_account()
{
    Events::insn_stall_account();

    if (this->iss.exec.cosim->enabled())
    {
        this->iss.exec.cosim->drain();
    }
}
