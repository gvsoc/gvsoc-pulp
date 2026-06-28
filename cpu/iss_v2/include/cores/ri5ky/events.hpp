// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/event/event.hpp>

class Ri5kyEvents : public Events
{
public:
    Ri5kyEvents(Iss &iss) : Events(iss) {}

    void reset(bool active);

    inline void event_cycle_enable();
    inline void event_cycle_disable();
    inline void event_imiss_account(int cycles);
    inline void event_imiss_start();
    inline void event_imiss_stop();
    inline void event_taken_branch_account();
    inline void event_instr_account();
    inline void event_load_account(int incr);
    inline void event_rvc_account(int incr);
    inline void event_store_account(int incr);
    inline void event_branch_account();
    inline void event_jump_account();
    inline void event_jalr_account(int rs1);
    inline void event_retire_account(iss_insn_t *insn);
    inline void event_insn_latency_account(iss_insn_t *insn, int latency);
    inline void event_div_account(iss_reg_t dividend, iss_reg_t divisor,
                                  bool is_signed, bool is_rem);
    inline void event_scoreboard_stall(uint8_t reason);
    inline void event_load_load_account(int incr);
    // p.elw wasted cycles: `elw_cycles` -> PCCR[11] (perf_pipeline_stall_o);
    // `gated_cycles` (the clock-gated park) -> PCCR[0], which RTL keeps
    // counting through ELW_EXE but GVSoC's busy_exit gates out.
    inline void event_elw_account(int elw_cycles, int gated_cycles);

    void flush_cycles();

private:

    int64_t imiss_start_cyclestamp;
    int64_t cycles_start_cyclestamp;
    // Destination register of the last retired instruction. Updated by
    // event_retire_account; consumed by event_jalr_account to detect
    // the jr_stall hazard (jalr's rs1 was just written by the previous
    // insn, mirroring the RTL controller's jr_stall_o on EX/WB/ALU
    // forward). -1 means "no recent producer" — set at reset and after
    // every jalr (since the taken-jump flush drains the pipeline).
    int prev_dest_reg;
};
