// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include "cpu/iss_v2/include/cores/ri5ky/csr.hpp"
#include <vp/vp.hpp>
#include <cpu/iss_v2/include/cores/ri5ky/events.hpp>
#include <cpu/iss_v2/include/event/event_implem.hpp>

inline void Ri5kyEvents::event_load_account(int incr)
{
    Events::event_load_account(incr);
    this->iss.csr.pccr_account(CSR_PCER_LD, incr);
}

inline void Ri5kyEvents::event_rvc_account(int incr)
{
    Events::event_rvc_account(incr);
    this->iss.csr.pccr_account(CSR_PCER_RVC, incr);
}

inline void Ri5kyEvents::event_store_account(int incr)
{
    Events::event_store_account(incr);
    this->iss.csr.pccr_account(CSR_PCER_ST, incr);
}

inline void Ri5kyEvents::event_branch_account()
{
    Events::event_branch_account();
    this->iss.csr.pccr_account(CSR_PCER_BRANCH, 1);
}

inline void Ri5kyEvents::event_load_load_account(int incr)
{
    Events::event_load_load_account(incr);
    this->iss.csr.pccr_account(CSR_PCER_LD_STALL, 1);
}

inline void Ri5kyEvents::event_elw_account(int elw_cycles, int gated_cycles)
{
    // Cycles wasted by a p.elw event load, charged to PCCR[11]
    // (CSR_PCER_ELW) to mirror RTL's perf_pipeline_stall_o.
    this->iss.csr.pccr_account(CSR_PCER_ELW, elw_cycles);
    // The clock-gated park does not advance PCCR[0] (busy_exit disables the
    // cycle event); RTL keeps the cycle counter running through ELW_EXE, so
    // add the gated span back to match.
    this->iss.csr.pccr_account(CSR_PCER_CYCLES, gated_cycles);
}

inline void Ri5kyEvents::event_instr_account()
{
    Events::event_instr_account();
    this->iss.csr.pccr_account(CSR_PCER_INSTR, 1);
}

inline void Ri5kyEvents::event_cycle_enable() {
    Events::event_cycle_enable();
    this->cycles_start_cyclestamp = this->iss.clock.get_cycles();

}

inline void Ri5kyEvents::event_cycle_disable() {
    Events::event_cycle_disable();
    this->iss.csr.pccr_account(CSR_PCER_CYCLES,
        this->iss.clock.get_cycles() - this->cycles_start_cyclestamp);
    this->cycles_start_cyclestamp = -1;
}

inline void Ri5kyEvents::event_imiss_account(int cycles)
{
    Events::event_imiss_account(cycles);
    this->iss.exec.stall_cycles_inc(cycles);
    this->iss.csr.pccr_account(CSR_PCER_IMISS, cycles);
}

inline void Ri5kyEvents::event_imiss_start()
{
    Events::event_imiss_start();
    this->imiss_start_cyclestamp = this->iss.clock.get_cycles();
}

inline void Ri5kyEvents::event_imiss_stop()
{
    Events::event_imiss_stop();
    this->iss.csr.pccr_account(CSR_PCER_IMISS,
        this->iss.clock.get_cycles() - this->imiss_start_cyclestamp);
}

inline void Ri5kyEvents::event_taken_branch_account()
{
    Events::event_taken_branch_account();
    this->iss.exec.stall_cycles_inc(2);
    this->iss.csr.pccr_account(CSR_PCER_TAKEN_BRANCH, 1);
}

inline void Ri5kyEvents::event_jump_account()
{
    Events::event_jump_account();
    this->iss.exec.stall_cycles_inc(1);
    this->iss.csr.pccr_account(CSR_PCER_JUMP, 1);
}

inline void Ri5kyEvents::event_jalr_account(int rs1)
{
    // Taken-jump pipeline flush — same one-cycle bubble as JAL.
    this->iss.exec.stall_cycles_inc(1);

    // RI5CY's jr_stall_o: if the source register is being written by an
    // in-flight instruction (still in EX/WB/ALU-forward at the time the
    // JALR enters ID), the controller deasserts write-enable for one
    // cycle until the producer's forward path clears. Match against the
    // last retired destination register; r0 / x0 is never a real
    // producer so we ignore it explicitly.
    //
    // PCER bookkeeping: while the JALR is held in ID by jr_stall the RTL
    // has id_valid_o = 0 (riscv_id_stage.sv:1782), and the cs_registers
    // PCCR_in[7] gate `& id_valid_q` (riscv_cs_registers.sv:1104) keeps
    // the JUMP counter from firing — so a stalled JALR shows up as a
    // JR_STALL event but NOT as a JUMP event. Mirror that here.
    if (rs1 != 0 && rs1 == this->prev_dest_reg)
    {
        this->iss.exec.stall_cycles_inc(1);
        this->iss.csr.pccr_account(CSR_PCER_JMP_STALL, 1);
    }
    else
    {
        this->iss.csr.pccr_account(CSR_PCER_JUMP, 1);
    }

    // The taken jump drains the pipeline behind us — clear the producer
    // history so the next JALR doesn't see a stale prev_dest from
    // before this flush.
    this->prev_dest_reg = -1;
}

inline void Ri5kyEvents::event_retire_account(iss_insn_t *insn)
{
    // Remember the destination register of the just-retired instruction
    // so a following jalr can detect the jr_stall hazard. Stores have
    // out_regs[0] == 0 (x0) which is fine — x0 can never be a real
    // producer.
    this->prev_dest_reg = (int)insn->out_regs[0];
}

inline void Ri5kyEvents::event_scoreboard_stall(uint8_t reason)
{
    // The scoreboard hands us the opaque reason byte that the producer
    // stored when it invalidated the blocking register. Map it onto
    // RI5CY's PCER counters. PCCR_LD_STALL pulses per load-use hazard
    // (riscv_controller.sv:1050-1065 → perf_ld_stall_o).
    if (reason == ISS_STALL_REASON_LOAD)
    {
        this->iss.csr.pccr_account(CSR_PCER_LD_STALL, 1);
    }
}

inline void Ri5kyEvents::event_insn_latency_account(iss_insn_t *insn,
                                                    int latency)
{
    // RI5CY's multi-cycle units (multiplier FSM, serial divider, …) drop
    // mult_ready / div_ready / similar to 0 during their internal cycles,
    // which gates ex_ready_o and then id_ready_o (riscv_id_stage.sv:1781).
    // The follower in ID can therefore not advance — regardless of any
    // data dependency on the result — so we charge the latency as an
    // unconditional structural stall. The value comes from the decoder
    // tagging done in Ri5ky::start().
    this->iss.exec.stall_cycles_inc(latency);
}

inline void Ri5kyEvents::event_div_account(iss_reg_t dividend, iss_reg_t divisor,
                                            bool is_signed, bool is_rem)
{
    // RI5CY's divider (riscv_alu_div.sv) is a serial bit-iteration unit
    // whose cycle count is dominated by the divisor's leading-zero
    // pre-shift (riscv_alu.sv:1035-1036). Per-div total cycles:
    //   * positive (or unsigned) divisor:  clz(divisor) + 3
    //   * zero divisor:                    35  (clz(0) treated as 32)
    //   * negative signed divisor:         clz(~divisor) + 2  (the RTL
    //     drops the +1 div_shift adjustment when div_op_a_signed=1).
    //
    // The follower in ID is held for the entire FSM (div_ready gates
    // ex_ready_o which gates id_ready_o), so the stall is structural
    // regardless of whether the result is used.
    int32_t d = (int32_t)divisor;
    int cycles;
    if (d == 0)
    {
        cycles = 35;
    }
    else if (is_signed && d < 0)
    {
        uint32_t inv = ~(uint32_t)d;
        int clz_inv = (inv == 0) ? 32 : __builtin_clz(inv);
        cycles = clz_inv + 2;
    }
    else
    {
        cycles = __builtin_clz((uint32_t)d) + 3;
    }

    // The div instruction itself already costs 1 dispatch cycle; charge
    // the rest as stall.
    this->iss.exec.stall_cycles_inc(cycles - 1);
}
