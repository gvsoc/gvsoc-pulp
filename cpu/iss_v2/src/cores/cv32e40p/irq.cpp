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

void Cv32e40pIrq::start()
{
    /* Registered here so it runs after IrqRiscv::mie_access (bound in the
     * base constructor), which stores the written value unmasked and
     * suppresses the register's own write mask. */
    this->iss.csr.mie.register_callback(std::bind(&Cv32e40pIrq::mie_write_fixup,
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
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
    /* Execute-address trigger (trigger module, mcontrol): the match fires
     * BEFORE the instruction at tdata2 executes (RTL trigger_match_o on
     * pc_id), entering debug with dcsr.cause=2 and dpc = the matched PC.
     * Evaluated at the dispatch boundary so the matched instruction is
     * never retired - a batched co-sim step cannot run past the entry.
     * Only the slow dispatch handler runs check(): the co-sim personality
     * pins it; a standalone fast-mode run does not evaluate triggers. */
    if ((this->iss.csr.tdata1.value & (1u << 2)) &&
        !this->iss.exec.debug_mode && !this->req_debug &&
        this->iss.exec.current_insn == this->iss.csr.tdata2.value)
    {
        this->req_debug = true;
        this->req_debug_cause = 2;
    }

    /* Debug entry: generic implementation plus dcsr.cause, written
     * atomically with the entry as the RTL does. The cause comes from
     * req_debug_cause: 3 (haltreq) on the wire path, 2 (trigger) from the
     * local execute-trigger match above, 1 (ebreak) or 4 (single-step)
     * when armed by an external debug-entry request. */
    if (this->req_debug && !this->iss.exec.debug_mode)
    {
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

    /* M-mode only core: the take needs a wired pending line and the global
     * enable (mstatus.MIE). */
    iss_reg_t pending = this->iss.csr.mie.value & this->iss.csr.mip.value & IRQ_MASK;
    if (!pending || !this->iss.csr.mstatus.mie)
    {
        return 0;
    }

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

    return 1;
}
