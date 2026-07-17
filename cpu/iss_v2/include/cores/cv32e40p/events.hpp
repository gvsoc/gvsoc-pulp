// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/event/event.hpp>

/* RTL hpm_events bit positions (cv32e40p_cs_registers.sv:1327). Only the
 * architectural, instruction-derived lines are accounted by the model; the
 * timing lines (cycle, stalls, imiss, APU) never fire. */
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
    inline void event_retire_account(iss_insn_t *insn);

private:
    /* Event lines fired by the executing instruction, committed as one OR
     * mask at retire: each counter advances at most +1 per instruction, as
     * the RTL advances at most +1 per cycle (cv32e40p_cs_registers.sv:1437).
     * Relies on the LSU firing its hooks only for accepted requests (no
     * hook before a stall is detected), so nothing leaks across retries. */
    uint32_t pending_events = 0;
};
