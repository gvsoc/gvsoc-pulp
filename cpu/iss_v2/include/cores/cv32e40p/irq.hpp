// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
// Declares InsnEntry, used by irq_riscv.hpp without including it.
#include <cpu/iss_v2/include/insn.hpp>
#include <cpu/iss_v2/include/irq/irq_riscv.hpp>

// RTL COREV_CLUSTER parameter, set by the core recipe (pulp/cv32e40p/cv32e40p.py).
#ifndef CONFIG_GVSOC_ISS_CV32E40P_COREV_CLUSTER
#define CONFIG_GVSOC_ISS_CV32E40P_COREV_CLUSTER 0
#endif

class Cv32e40pIrq : public IrqRiscv
{
public:
    /* Interrupt lines of the RTL: MSI(3), MTI(7), MEI(11) and the fast lines
     * irq[31:16] (cv32e40p_int_controller.sv, IRQ_MASK). */
    static constexpr iss_reg_t IRQ_MASK = 0xFFFF0888;

    // Registers the haltreq (RTL debug_req_i) and mtvec_addr ports.
    Cv32e40pIrq(Iss &iss);

    void start();

    // Shadows IrqRiscv::reset (CONFIG_GVSOC_ISS_IRQ).
    void reset(bool active);

    /* Debug entry and interrupt take with the RTL priorities and vectored
     * entry. A pending debug request is left to dispatch_decide(). In
     * co-simulation decide() runs instead, at the RTL decision points. */
    int check();

    /* Called at the dispatch of a decoded instruction. Serves the debug
     * request that check() left pending, whose cause depends on the
     * instruction in ID. Returns true when the PC moved. */
    inline bool dispatch_decide(iss_insn_t *insn)
    {
        if (!this->decide_at_dispatch)
        {
            return false;
        }
        this->decide_at_dispatch = false;
        return this->dispatch_check(insn) != 0;
    }

    /* Co-simulation inputs, applied in RTL order. input_set() gives the pin
     * levels, input_sample() the sample of one input domain and decide() a
     * decision point of the controller. decide() returns 1 when the core took
     * an interrupt, entered debug mode or woke up. */
    void input_set(uint32_t irq_level, bool debug_req);
    void input_sample(uint32_t domain);
    int decide(uint32_t kind);

    /* RTL debug_req_pending: the pin, its latch, or a request armed by the
     * haltreq wire, a trigger or a single step. */
    bool debug_request_pending() const
    {
        return this->req_debug || this->haltreq_level || this->haltreq_latch;
    }

    // The haltreq wire arms req_debug and wakes a hart sleeping in WFI.
    static void haltreq_sync(vp::Block *__this, bool value);
    /* The interrupt lines, as IrqRiscv drives them, plus the wake-up of a
     * core sleeping on a cv.elw (ELW_EXE leaves on an interrupt request). */
    static void msi_sync(vp::Block *__this, bool value);
    static void mti_sync(vp::Block *__this, bool value);
    static void mei_sync(vp::Block *__this, bool value);
    static void external_irq_sync(vp::Block *__this, bool value, int id);
    /* The mtvec_addr wire (RTL mtvec_addr_i) gives the mtvec base at boot,
     * which the generic reset would take from the boot address. Until it is
     * driven, the base is the mtvec_addr property (core config, 0 by
     * default). */
    static void mtvec_addr_sync(vp::Block *__this, uint32_t value);

    /* An ebreak outside debug mode with dcsr.ebreakm=1 (RTL DBG_TAKEN_ID with
     * the ebreak in ID) enters debug mode with dpc = pc, leaving mcause and
     * mepc unchanged. The ebreak is the entry itself, and its trapped record
     * carries dcsr and dpc, as RVFI reports it. Returns the debug handler. */
    iss_reg_t ebreak_debug_entry(iss_reg_t pc);

    /* Set by an ebreak that enters or re-enters the debug handler: it does
     * not retire, so at a loop end it does not update the loop
     * (Cv32e40pHwloop::check). */
    bool ebreak_to_debug = false;

    /* dret with dcsr.step=1 (priv.hpp dret_exec) opens the single-step
     * window, and debug mode is entered again with cause 4 after one
     * instruction. */
    void dret_step_check();

    /* Single-step window, 0 when idle and 1 while stepping step_pc. It
     * closes when current_insn != step_pc, after the stepped instruction or
     * at the handler of its exception, so a jump to itself keeps it open. An
     * interrupt taken in the window moves step_pc to its handler (irq_take). */
    int step_state = 0;
    iss_reg_t step_pc = 0;

    /* Level of the haltreq wire. RTL debug_req_i is level-sensitive and the
     * wire syncs on changes only, so check() re-arms req_debug from it. */
    bool haltreq_level = false;
    // RTL debug_req_q, loaded with the pin at each debug-domain sample (co-simulation).
    bool haltreq_latch = false;
    // irq_i levels, before the gated-clock sample (co-simulation).
    uint32_t irq_level = 0;

    // dcsr.cause of the next debug entry: 1 ebreak, 2 trigger, 3 haltreq, 4 step.
    int req_debug_cause = 3;
    // check() left the debug request to dispatch_decide().
    bool decide_at_dispatch = false;
    /* The haltreq wire woke the hart. The RTL then leaves the sleep through
     * DBG_TAKEN_IF, without an instruction in ID. */
    bool debug_wakeup = false;

    vp::WireSlave<bool> haltreq_itf;
    uint32_t mtvec_addr = 0;
    vp::WireSlave<uint32_t> mtvec_addr_itf;
    /* RTL irq_ack_o / irq_id_o: pulsed with the identifier of each interrupt
     * taken, for an interrupt controller such as the PULP event unit, which
     * clears its request on it. */
    vp::WireMaster<int> irq_ack_itf;

private:
    void mtvec_init();
    bool mie_write_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value);

    // Decision in DECODE, with insn the instruction in ID (NULL when not decoded).
    int dispatch_check(iss_insn_t *insn);
    // RTL ebrk_force_debug_mode & ebrk_insn_i.
    bool ebreak_enters_debug(iss_insn_t *insn) const;
    // The decoded instruction at the current PC, NULL when not decoded.
    iss_insn_t *id_insn();
    // Interrupt part of dispatch_check(), alone in FIRST_FETCH.
    int irq_check();
    // Wake-up decision in SLEEP.
    int sleep_check();
    // Debug entry for the armed request, before current_insn.
    int debug_enter();
    void debug_state_enter(int cause, iss_reg_t dpc);
    // Interrupt take (RTL priority, vectored entry).
    void irq_take(iss_reg_t pending);

    // Wakes the hart from WFI, as IrqRiscv::check_interrupts() does.
    void release_wfi();
    // Wakes a core sleeping on a cv.elw if a line would now be taken.
    void elw_irq_check();
};
