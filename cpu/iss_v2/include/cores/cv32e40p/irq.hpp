// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/irq/irq_riscv.hpp>

class Cv32e40pIrq : public IrqRiscv
{
public:
    /* Interrupt lines wired in the RTL: MSI(3), MTI(7), MEI(11) and the
     * sixteen fast lines irq[31:16] (cv32e40p_int_controller.sv IRQ_MASK). */
    static constexpr iss_reg_t IRQ_MASK = 0xFFFF0888;

    /* Defined in irq.cpp (Iss is incomplete here): registers the haltreq
     * slave port - the debug halt request line (RTL debug_req_i), a
     * first-class wire like the interrupt lines, handled by haltreq_sync. */
    Cv32e40pIrq(Iss &iss);

    void start();

    /* Shadows IrqRiscv::reset (static dispatch via CONFIG_GVSOC_ISS_IRQ,
     * iss.cpp): the base reset does not know the personality's state. A
     * live single-step window (step_state/step_pc) or an unconsumed
     * collision id surviving a reset would fire a phantom cause=4 entry
     * or a stale interrupt take on the first post-reset boundary. */
    void reset(bool active);

    /* Interrupt take with the RTL priority order and vectored entry;
     * shadows the generic RISC-V ladder (static dispatch via
     * CONFIG_GVSOC_ISS_IRQ). */
    int check();

    /* haltreq wire: arms req_debug and wakes a WFI-parked hart. */
    static void haltreq_sync(vp::Block *__this, bool value);

    /* wfi_wake wire: releases a WFI-parked hart (full three-step release,
     * only callable inside the model) with NO architectural side effect.
     * Driven externally when the DUT's retire stream proves the wake
     * happened (the RTL retires wfi at execute and sleeps after;
     * wake sources like debug_req are not all visible as interrupt
     * wires). */
    static void wfi_wake_sync(vp::Block *__this, bool value);

    /* ebreak with dcsr.ebreakm=1 outside debug mode (isa/rv32i.hpp,
     * isa/rv32c.hpp): arms the debug request; check() performs the entry
     * at the next dispatch boundary, so the ebreak never retires and
     * mcause/mepc stay untouched - like the RTL, where the entry row is
     * the first debug-ROM instruction. Inline: touches members only
     * (Iss is incomplete here). */
    void ebreak_enter_debug()
    {
        this->req_debug = true;
        this->req_debug_cause = 1;
    }

    /* dret with dcsr.step=1 (priv.hpp dret_exec): opens the single-step
     * window. check() re-enters debug with cause=4 once the stepped
     * instruction is done. Defined in irq.cpp (reads dcsr/depc). */
    void dret_step_check();

    /* Single-step window state: 0 = idle, 1 = stepping (step_pc holds the
     * address of the one instruction to execute). The exit condition in
     * check() is current_insn != step_pc: a completed instruction moved
     * the PC, and an exception redirect (has_exception, consumed before
     * check() runs) lands the entry on the handler address, as the Debug
     * spec requires. A stepped jump-to-self never trips it; that corner
     * is handled by an externally armed entry. */
    int step_state = 0;
    iss_reg_t step_pc = 0;

    /* Level of the haltreq wire (RTL debug_req_i is level-sensitive: while
     * high the hart re-halts right after dret). haltreq_sync records it;
     * check() re-arms req_debug from it outside debug mode, since the wire
     * only syncs on level CHANGES and a still-high level after an entry
     * consumed req_debug would otherwise be lost. */
    bool haltreq_level = false;

    /* dcsr.cause for the next req_debug take (debug spec: 1=ebreak,
     * 3=haltreq, 4=single-step). The haltreq wire path leaves the
     * default; an external debug-entry request sets it from the DUT's
     * dcsr before arming req_debug. Reset to 3 by the entry itself. */
    int req_debug_cause = 3;

    /* Informed interrupt+debug collision (co-sim): when the DUT's debug
     * entry row carries the CSR writes of an interrupt take, the
     * external driver stores the taken cause id here before arming the
     * entry; check() then takes exactly that line ahead of the entry, so
     * dpc lands on the (vectored) handler entry and mstatus/mepc/mcause
     * carry the take, as in the RTL. -1 = no collision. The model never
     * guesses this on its own: the arbitration outcome depends on cycle
     * timing only the DUT can observe. */
    int collide_irq_id = -1;

    vp::WireSlave<bool> haltreq_itf;
    vp::WireSlave<bool> wfi_wake_itf;

private:
    bool mie_write_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value);

    /* Interrupt take (RTL priority + vectored entry); shared by the
     * check() ladder and the simultaneous-interrupt debug entry. */
    void irq_take(iss_reg_t pending);

    /* Full WFI release (flag clear, retain_dec, terminate of the held
     * entry - the terminated entry drains into the commit stream). Only
     * callable inside the model; shared by haltreq_sync and
     * wfi_wake_sync. No-op when the hart is not parked. */
    void release_wfi();
};
