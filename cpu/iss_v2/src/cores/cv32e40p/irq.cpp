// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

/* CV32E40P interrupt personality for iss_v2.
 *
 * The generic IrqRiscv take (irq_riscv.cpp check()) uses the standard
 * RISC-V priority ladder and jumps to the mtvec base for every interrupt.
 * The RTL differs on both counts (cv32e40p_int_controller.sv,
 * cv32e40p_controller.sv): the fast lines irq[31:16] outrank MEI/MSI/MTI,
 * and vectored mode sends each interrupt to base + 4*id. This override
 * implements the RTL behaviour; delivery (mip update, WFI wake-up) stays
 * on the inherited wire-sync path. */

#include <cpu/iss_v2/include/cores/cv32e40p/irq.hpp>
#include <cpu/iss_v2/include/iss.hpp>

/* Debug halt request line (RTL debug_req_i), a first-class wire like the
 * interrupt lines. Handling it inside the model lets the model run the
 * full RTL semantics - in particular waking a WFI-parked hart: the RTL
 * sleep unit exits on debug_req_i regardless of mie/mip, while the generic
 * check_interrupts() release is gated on (mie & mip) alone. */
Cv32e40pIrq::Cv32e40pIrq(Iss &iss) : IrqRiscv(iss)
{
    this->haltreq_itf.set_sync_meth(&Cv32e40pIrq::haltreq_sync);
    this->iss.new_slave_port("haltreq", &this->haltreq_itf, (vp::Block *)this);
    this->wfi_wake_itf.set_sync_meth(&Cv32e40pIrq::wfi_wake_sync);
    this->iss.new_slave_port("wfi_wake", &this->wfi_wake_itf, (vp::Block *)this);
}

void Cv32e40pIrq::start()
{
    /* Registered here so it runs after IrqRiscv::mie_access (bound in the
     * base constructor), which stores the written value unmasked and
     * suppresses the register's own write mask. */
    this->iss.csr.mie.register_callback(std::bind(&Cv32e40pIrq::mie_write_fixup,
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void Cv32e40pIrq::reset(bool active)
{
    IrqRiscv::reset(active);

    if (active)
    {
        /* A live single-step window or an unconsumed collision id must not
         * survive the reset: stale step_pc would fire a phantom cause=4
         * entry at the boot address, a stale collision id would make the
         * first post-reset debug entry take an interrupt that never
         * happened. haltreq_level is deliberately kept: it mirrors the
         * actual wire level, which the reset does not change. */
        this->step_state = 0;
        this->collide_irq_id = -1;
        this->collide_certify = false;
        this->req_debug_cause = 3;
    }
}

/* Full WFI release: the same three-step sequence as check_interrupts() -
 * the held WFI entry must drain into the commit stream, a bare wfi-flag
 * clear would leave it parked forever. */
void Cv32e40pIrq::release_wfi()
{
    if (this->iss.exec.wfi.get())
    {
        this->iss.exec.wfi.set(false);
        this->iss.exec.retain_dec();
        this->iss.exec.insn_terminate(this->wfi_entry);
    }
}

/* Debug halt request wire (RTL debug_req_i).
 *
 * Arms req_debug - consumed by check() at the next dispatch, exactly like
 * an externally armed request - and wakes a WFI-parked hart: the
 * RTL sleep unit exits sleep on debug_req_i regardless of pending
 * interrupts (cv32e40p_sleep_unit.sv / controller wake-up), while the
 * generic check_interrupts() release is gated on (mie & mip) alone, so a
 * halt request arriving with mie=0 would otherwise never wake the model. */
void Cv32e40pIrq::haltreq_sync(vp::Block *__this, bool value)
{
    Cv32e40pIrq *_this = (Cv32e40pIrq *)__this;

    _this->haltreq_level = value;  /* check() re-arms from a held-high level */

    if (!value)
    {
        return;  /* level deassert: an armed req_debug stays latched */
    }

    _this->req_debug = true;  /* req_debug_cause keeps its default (3 = haltreq) */

    _this->release_wfi();
}

/* wfi_wake wire: releases a WFI-parked hart with no architectural side
 * effect (release_wfi is only callable inside the model); driven
 * externally when the DUT's retire stream proves the wake happened
 * - the RTL retires wfi at execute and sleeps after, and wake sources
 * like a debug_req level are not all visible as interrupt wires. The
 * terminated entry drains into the commit stream, serving the DUT's own
 * wfi retire. */
void Cv32e40pIrq::wfi_wake_sync(vp::Block *__this, bool value)
{
    Cv32e40pIrq *_this = (Cv32e40pIrq *)__this;

    if (!value)
    {
        return;  /* deassert edge of the pulse */
    }

    _this->release_wfi();
}

/* dret with dcsr.step=1 (priv.hpp dret_exec, called before dret_handle
 * while depc is still live): opens the single-step window. The RTL
 * controller re-enters debug after one instruction (cv32e40p_controller.sv
 * debug_single_step_i); the matching entry is armed by check() once the
 * stepped instruction is done. */
void Cv32e40pIrq::dret_step_check()
{
    if ((this->iss.csr.dcsr >> 2) & 1)
    {
        this->step_state = 1;
        this->step_pc = this->iss.csr.depc;
    }
}

/* RTL WARL result: only the wired interrupt lines are writable in mie
 * (cv32e40p_cs_registers.sv, csr_mie_wdata & IRQ_MASK). */
bool Cv32e40pIrq::mie_write_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (!is_write)
    {
        return true;
    }
    this->iss.csr.mie.value &= IRQ_MASK;
    return false;
}

/* RTL priority: irq[31] highest, down to irq[16], then MEI(11), MSI(3),
 * MTI(7). The caller guarantees at least one bit of IRQ_MASK is set. */
static int cv32e40p_irq_pick(iss_reg_t pending)
{
    for (int id = 31; id >= 16; id--)
    {
        if ((pending >> id) & 1)
        {
            return id;
        }
    }
    if ((pending >> 11) & 1) return 11;
    if ((pending >> 3) & 1)  return 3;
    return 7;
}

int Cv32e40pIrq::check()
{
    /* Cause arbitration on a shared entry boundary follows the RTL, which
     * has TWO distinct entry paths:
     *  - DBG_TAKEN_ID (kill of the ID insn): TRIGGER (highest) > EBREAK >
     *    HALTREQ. The trigger block therefore OVERRIDES an armed haltreq -
     *    e.g. the async wire edge that latched before the matched boundary;
     *    the level re-arm serves it after dret, like the RTL re-halt.
     *  - DBG_TAKEN_IF (single-step window close): its cause mux never
     *    looks at trigger_match, so an armed STEP request (cause 4) is
     *    NOT overridden even when the next insn sits at tdata2 - the
     *    trigger fires on the following session instead. An armed cause
     *    1/2 (injected ebreak/trigger, DUT-observed) is left alone too. */

    /* Execute-address trigger (trigger module, mcontrol): the match fires
     * BEFORE the instruction at tdata2 executes (RTL trigger_match_o on
     * pc_id), entering debug with dcsr.cause=2 and dpc = the matched PC.
     * Evaluated at the dispatch boundary so the matched instruction is
     * never retired - a batched co-sim step cannot run past the entry.
     * Only the slow dispatch handler runs check(): the co-sim personality
     * pins it; a standalone fast-mode run does not evaluate triggers. */
    /* The step-window guard mirrors DBG_TAKEN_IF: when the window closes
     * at this boundary (armed and the stepped insn is done) the step entry
     * wins even if the NEXT insn sits at tdata2 - the trigger block runs
     * first in program order, so it must yield explicitly. A window still
     * on its own step_pc (dret straight onto the matched insn) does not
     * close here and the trigger fires as on the RTL. */
    bool trigger_match =
        (this->iss.csr.tdata1.value & (1u << 2)) &&
        !this->iss.exec.debug_mode &&
        (!this->req_debug || this->req_debug_cause == 3) &&
        !(this->step_state && this->iss.exec.current_insn != this->step_pc) &&
        this->iss.exec.current_insn == this->iss.csr.tdata2.value;
    if (trigger_match)
    {
        this->req_debug = true;
        this->req_debug_cause = 2;
    }

    /* One-shot async gate, consumed AFTER the trigger match: the execute
     * trigger is synchronous debug (mcontrol timing=before), not an
     * asynchronous event, so it fires even on a suppressed dispatch and
     * falls through to the entry below. Everything past this point -
     * haltreq wire re-arm, step window, interrupt ladder - is asynchronous
     * and stays out of a suppressed boundary (DPI lockstep stepping: the
     * engine holds the line high and injects takes explicitly). */
    if (this->iss.exec.skip_irq_check)
    {
        this->iss.exec.skip_irq_check = false;
        if (!trigger_match)
        {
            return 0;
        }
    }

    /* Held-high haltreq re-arms (RTL debug_req_i is level-sensitive: the
     * hart re-halts right after dret while the line stays asserted; the
     * wire itself only syncs on level changes). */
    if (this->haltreq_level && !this->req_debug && !this->iss.exec.debug_mode)
    {
        this->req_debug = true;  /* req_debug_cause keeps its default (3) */
    }

    /* Single-step window (dcsr.step, armed by dret_step_check): re-enter
     * debug with cause=4 once the stepped instruction is done. "Done" is
     * current_insn != step_pc: a completed instruction moved the PC, and
     * an exception during the step lands here after the has_exception
     * redirect (consumed at the top of the dispatch, before check()), so
     * depc points at the handler entry, as the Debug spec requires. While
     * current_insn == step_pc the instruction has not executed yet (fetch
     * or scoreboard stalls re-run check() on the same boundary). The
     * window is closed by the entry itself, whatever the winning cause. */
    if (this->step_state && !this->iss.exec.debug_mode && !this->req_debug)
    {
        if (this->iss.exec.current_insn != this->step_pc)
        {
            this->req_debug = true;
            this->req_debug_cause = 4;
        }
    }

    /* Debug entry: generic implementation plus dcsr.cause, written
     * atomically with the entry as the RTL does. The cause comes from
     * req_debug_cause: 3 (haltreq) on the wire path, 2 (trigger) from the
     * local execute-trigger match above, 1 (ebreak) or 4 (single-step)
     * when armed by an external debug-entry request. */
    if (this->req_debug && !this->iss.exec.debug_mode)
    {
        /* Informed interrupt+debug collision: the RTL takes the interrupt
         * first and enters debug on the first handler instruction - dpc is
         * the (vectored) entry and mstatus/mepc/mcause carry the take; the
         * handler instruction itself never executes. Whether the collision
         * happened is decided by the DUT (its entry row carries the take's
         * CSR writes), never guessed here: the arbitration outcome depends
         * on cycle timing the model cannot see. Both trap_seq bumps land
         * before debug_mode flips, so an external observer sees one
         * atomic entry. */
        if (this->collide_irq_id >= 0)
        {
            /* Defense in depth: irq_take's
             * priority pick falls back to MTI when no IRQ_MASK bit is set,
             * so an unwired id would silently become a phantom cause-7
             * take. The id is DUT-provided (mcause & 0x1f), never trusted
             * blindly. */
            iss_reg_t line = (this->collide_irq_id < 32) ?
                (((iss_reg_t)1 << this->collide_irq_id) & IRQ_MASK) : 0;
            /* Adjacent-row candidates carry the take's mepc and are
             * certified HERE, where current_insn is the entry boundary
             * (the future depc source): a stale-mcause candidate - a take
             * this hart already followed rows ago - parks the boundary
             * elsewhere and is discarded without a take. Same-row
             * candidates (collide_certify=false) keep the unconditional
             * behaviour. */
            if (this->collide_certify &&
                this->iss.exec.current_insn != this->collide_expected_mepc)
            {
                this->trace.msg(vp::Trace::LEVEL_WARNING,
                    "Informed IRQ+debug collision id %d discarded: entry "
                    "boundary 0x%x != take mepc 0x%x\n",
                    this->collide_irq_id,
                    (unsigned)this->iss.exec.current_insn,
                    (unsigned)this->collide_expected_mepc);
            }
            else if (line)
            {
                this->irq_take(line);
            }
            else
            {
                this->trace.msg(vp::Trace::LEVEL_WARNING,
                    "Informed IRQ+debug collision with unwired cause id %d - "
                    "take skipped\n", this->collide_irq_id);
            }
            this->collide_irq_id = -1;
            this->collide_certify = false;
        }

        /* Any entry closes a live single-step window: without this, a
         * haltreq during the window (or a dret whose debugger cleared
         * dcsr.step) leaves step_state armed on a stale step_pc and the
         * next boundary would fire a spurious cause=4 entry. */
        this->step_state = 0;
        this->iss.exec.debug_mode = true;
        this->iss.csr.depc = this->iss.exec.current_insn;
        this->iss.csr.dcsr = (this->iss.csr.dcsr & ~(0x7u << 6)) |
                             ((iss_reg_t)(this->req_debug_cause & 0x7) << 6);
        this->req_debug_cause = 3;
        /* Commit-stream consumers gate state compares on this (events.hpp). */
        this->iss.timing.trap_seq++;
        this->debug_saved_irq_enable = this->irq_enable.get();
        this->irq_enable.set(0);
        this->req_debug = false;
        this->iss.exec.current_insn = this->debug_handler;
        return 1;
    }

    /* No interrupt is taken in debug mode (the RTL controller ignores
     * irq_req entirely there). Explicit guard: inside the take_debug
     * injection window the defense is down until the first debug-ROM
     * commit lands, so check() can run again right after the entry and
     * the ladder below would hijack it with a pending line. */
    if (this->iss.exec.debug_mode)
    {
        return 0;
    }

    /* Inside the single-step window interrupts are masked unless
     * dcsr.stepie=1 (bit 11): the RTL controller holds irq_req off while
     * single-stepping (cv32e40p_controller.sv debug_single_step_i). */
    if (this->step_state && !((this->iss.csr.dcsr >> 11) & 1))
    {
        return 0;
    }

    /* M-mode only core: the take needs a wired pending line and the global
     * enable (mstatus.MIE). */
    iss_reg_t pending = this->iss.csr.mie.value & this->iss.csr.mip.value & IRQ_MASK;
    if (!pending || !this->iss.csr.mstatus.mie)
    {
        return 0;
    }

    this->irq_take(pending);

    return 1;
}

/* Interrupt take with the RTL priority order and vectored entry. The
 * caller guarantees at least one bit of pending (mie & mip & IRQ_MASK)
 * and mstatus.MIE=1. Shared by the ladder in check() and the
 * simultaneous-interrupt path of the debug entry. */
void Cv32e40pIrq::irq_take(iss_reg_t pending)
{
    int irq = cv32e40p_irq_pick(pending);

    /* mtvec holds {base[31:8], 0, mode} (Cv32e40pCsr::mtvec_write_fixup);
     * vectored mode enters at base + 4*id, direct mode at base. */
    iss_reg_t base = this->iss.csr.mtvec.value & 0xFFFFFF00;
    iss_reg_t entry = (this->iss.csr.mtvec.value & 1) ? base + (irq << 2) : base;

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Handling IRQ (irq: %d, entry: 0x%lx)\n",
                    irq, entry);

    /* Commit-stream consumers gate state compares on this (events.hpp). */
    this->iss.timing.trap_seq++;

    this->iss.exec.interrupt_taken();
    this->iss.csr.mepc.value = this->iss.exec.current_insn;
    this->iss.csr.mstatus.mpie = this->iss.csr.mstatus.mie;
    this->iss.csr.mstatus.mie = 0;
    this->iss.csr.mstatus.mpp = this->iss.core.mode_get();
    this->iss.csr.mcause.value = (1ULL << (ISS_REG_WIDTH - 1)) | (unsigned int)irq;
    this->iss.exec.current_insn = entry;
    this->iss.core.mode_set(PRIV_M);
    this->irq_enable.set(0);

    this->iss.timing.stall_insn_dependency_account(4);
}
