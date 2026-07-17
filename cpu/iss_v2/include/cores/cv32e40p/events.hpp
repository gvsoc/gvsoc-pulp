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
    inline void insn_stall_account();

    /* Architectural commit stream for an external stepper (RVVI bridge).
     * A PC is pushed when the instruction's result is architecturally
     * visible: at retire for sync instructions, at commit-FIFO drain for
     * held ones (async load, WFI) - insn_stall_account only fires there -
     * where the writeback has already happened. The stepper pops. Sampling
     * the regfile on the raw retire hook instead would race the load
     * writeback (the LSU response lands one cycle later). */
    static constexpr int COMMIT_RING = 64;
    uint64_t commit_push = 0;
    uint64_t commit_pop = 0;
    iss_reg_t commit_pc[COMMIT_RING];

    /* Drop commits not consumed yet (external resync forced a new PC). */
    inline void commit_stream_flush();

private:
    /* Program-order PCs of the instructions parked in the exec commit
     * FIFO (held or sync follower); drain pops them in the same order. */
    uint64_t inflight_push = 0;
    uint64_t inflight_pop = 0;
    iss_reg_t inflight_pc[COMMIT_RING];

    /* Event lines fired by the executing instruction, committed as one OR
     * mask at retire: each counter advances at most +1 per instruction, as
     * the RTL advances at most +1 per cycle (cv32e40p_cs_registers.sv:1437).
     * Relies on the LSU firing its hooks only for accepted requests (no
     * hook before a stall is detected), so nothing leaks across retries. */
    uint32_t pending_events = 0;
};
