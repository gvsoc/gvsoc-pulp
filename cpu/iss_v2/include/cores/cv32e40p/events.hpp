// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/event/event.hpp>

/* Bits of the RTL hpm_events (cv32e40p_cs_registers.sv). The model counts
 * only the lines of the instructions, so the timing lines (cycle, stalls,
 * imiss, APU) never fire. */
#define CV32E40P_HPM_INSTR        (1u << 1)
#define CV32E40P_HPM_LD           (1u << 5)
#define CV32E40P_HPM_ST           (1u << 6)
#define CV32E40P_HPM_JUMP         (1u << 7)
#define CV32E40P_HPM_BRANCH       (1u << 8)
#define CV32E40P_HPM_BRANCH_TAKEN (1u << 9)
#define CV32E40P_HPM_COMP_INSTR   (1u << 10)

class Cv32e40pEvents : public Events
{
public:
    Cv32e40pEvents(Iss &iss) : Events(iss) {}

    void reset(bool active);

    inline void event_load_account(int incr);
    inline void event_store_account(int incr);
    inline void event_branch_account();
    inline void event_taken_branch_account();
    inline void event_jump_account();
    inline void event_jalr_account(int rs1);
    inline void event_retire_account(iss_insn_t *insn);
    // Called for each instruction leaving the commit FIFO (ExecInOrder::drain_entry).
    inline void insn_stall_account();

private:
    /* Event lines fired by the executing instruction, counted at its retire,
     * each once at most as in the RTL (cv32e40p_cs_registers.sv). The LSU
     * fires its hooks for accepted requests only, so a retry adds nothing. */
    uint32_t pending_events = 0;
};
