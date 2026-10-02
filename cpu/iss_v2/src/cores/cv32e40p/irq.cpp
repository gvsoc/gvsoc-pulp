// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

/* Interrupts and debug entry of the CV32E40P (cv32e40p_int_controller.sv,
 * cv32e40p_controller.sv). The fast lines irq[31:16] come before MEI, MSI
 * and MTI, and vectored mode enters at base + 4 * id. The pins reach mip
 * through the IrqRiscv wires, except in co-simulation, where the checker
 * reports the pins, their samples and the decision points of the RTL. */

#include <cpu/iss_v2/include/cores/cv32e40p/irq.hpp>
#include <cpu/iss_v2/include/iss.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>

Cv32e40pIrq::Cv32e40pIrq(Iss &iss) : IrqRiscv(iss)
{
    this->haltreq_itf.set_sync_meth(&Cv32e40pIrq::haltreq_sync);
    this->iss.new_slave_port("haltreq", &this->haltreq_itf, (vp::Block *)this);
    this->mtvec_addr_itf.set_sync_meth(&Cv32e40pIrq::mtvec_addr_sync);
    this->iss.new_slave_port("mtvec_addr", &this->mtvec_addr_itf, (vp::Block *)this);
}

void Cv32e40pIrq::start()
{
    /* Runs after IrqRiscv::mie_access, registered by the base constructor,
     * which stores the written value unmasked. */
    this->iss.csr.mie.register_callback(std::bind(&Cv32e40pIrq::mie_write_fixup,
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void Cv32e40pIrq::reset(bool active)
{
    IrqRiscv::reset(active);

    if (active)
    {
        /* The single-step window closes. haltreq_level is the wire level,
         * which the reset does not change. */
        this->step_state = 0;
        this->req_debug_cause = 3;
        this->decide_at_dispatch = false;
        this->debug_wakeup = false;
    }
    else
    {
        this->mtvec_init();
    }
}

void Cv32e40pIrq::mtvec_init()
{
    // Cv32e40pCsr::mtvec_write_fixup sets the mode bits (insn == NULL).
    iss_reg_t base = this->mtvec_addr & ~(iss_reg_t)0xFF;
    this->iss.csr.mtvec.access(NULL, true, base);
}

void Cv32e40pIrq::mtvec_addr_sync(vp::Block *__this, uint32_t value)
{
    Cv32e40pIrq *_this = (Cv32e40pIrq *)__this;
    _this->mtvec_addr = value;
    _this->mtvec_init();
}

/* Releases the WFI as IrqRiscv::check_interrupts() does. Clearing the flag
 * alone would leave the WFI entry held. */
void Cv32e40pIrq::release_wfi()
{
    if (this->iss.exec.wfi.get())
    {
        this->iss.exec.wfi.set(false);
        this->iss.exec.retain_dec();
        this->iss.exec.insn_terminate(this->wfi_entry);
    }
}

/* Debug halt request wire (RTL debug_req_i). Arms req_debug and wakes a
 * hart sleeping in WFI, because the RTL sleep unit wakes up on debug_req_i
 * whatever mie and mip (cv32e40p_sleep_unit.sv). */
void Cv32e40pIrq::haltreq_sync(vp::Block *__this, bool value)
{
    Cv32e40pIrq *_this = (Cv32e40pIrq *)__this;

    _this->haltreq_level = value;

    if (!value)
    {
        return;  // An armed req_debug stays set.
    }

    _this->req_debug = true;
    if (_this->iss.exec.wfi.get())
    {
        _this->debug_wakeup = true;
        _this->release_wfi();
    }
}

/* Called before dret_handle, while dpc holds the return address. The RTL
 * enters debug mode again after one instruction (cv32e40p_controller.sv,
 * debug_single_step_i). */
void Cv32e40pIrq::dret_step_check()
{
    if ((this->iss.csr.dcsr >> 2) & 1)
    {
        this->step_state = 1;
        this->step_pc = this->iss.csr.depc;
    }
}

/* Only the wired interrupt lines are writable in mie
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

/* RTL priority, from irq[31] down to irq[16], then MEI(11), MSI(3) and
 * MTI(7). pending has at least one bit of IRQ_MASK set. */
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
    if (this->iss.exec.cosim->enabled())
    {
        return 0;
    }
    /* check() runs before the fetch, but a debug request in DECODE takes its
     * cause from the instruction in ID, so the dispatch serves it. After a
     * wake-up the entry comes before the next instruction (DBG_TAKEN_IF). */
    if (this->debug_request_pending() && !this->iss.exec.debug_mode && !this->debug_wakeup)
    {
        this->decide_at_dispatch = true;
        return 0;
    }
    this->debug_wakeup = false;
    return this->dispatch_check(NULL);
}

void Cv32e40pIrq::input_set(uint32_t irq_level, bool debug_req)
{
    this->irq_level = irq_level & IRQ_MASK;
    this->haltreq_level = debug_req;
}

void Cv32e40pIrq::input_sample(uint32_t domain)
{
    if (domain == CV32E40P_COSIM_DOMAIN_IRQ)
    {
        // mip reads the irq_q flop (cv32e40p_int_controller.sv mip_o).
        this->iss.csr.mip.value = (this->iss.csr.mip.value & ~IRQ_MASK) | this->irq_level;
    }
    else
    {
        // debug_req_q loads the pin (cv32e40p_controller.sv).
        this->haltreq_latch = this->haltreq_level;
    }
}

int Cv32e40pIrq::decide(uint32_t kind)
{
    switch (kind)
    {
        case CV32E40P_COSIM_OPP_DISPATCH:
            // Reported for the instruction at the current PC.
            return this->dispatch_check(this->id_insn());
        case CV32E40P_COSIM_OPP_FIRST_FETCH:
            // A pending debug request is served in DECODE, one state later.
            if (this->debug_request_pending())
            {
                return 0;
            }
            return this->irq_check();
        case CV32E40P_COSIM_OPP_SLEEP:
            return this->sleep_check();
        case CV32E40P_COSIM_OPP_BOOT:
            if (!this->debug_request_pending())
            {
                return 0;
            }
            this->req_debug = true;
            return this->debug_enter();
    }
    return 0;
}

int Cv32e40pIrq::dispatch_check(iss_insn_t *insn)
{
    /* The RTL enters debug mode through DBG_TAKEN_ID, which kills the
     * instruction in ID, with the trigger, ebreak or haltreq cause in that
     * order. After a single step it enters through DBG_TAKEN_IF with the step
     * cause, even at tdata2, and a halt request raised during the step waits
     * for the dret. */
    const bool step_completed = this->step_state &&
        this->iss.exec.current_insn != this->step_pc;

    /* Execute-address trigger (RTL trigger_match_o on pc_id). Debug mode is
     * entered before the instruction at tdata2, with dpc at its address. */
    bool trigger_match =
        (this->iss.csr.tdata1.value & (1u << 2)) &&
        !this->iss.exec.debug_mode &&
        (!this->req_debug || this->req_debug_cause == 3) &&
        !step_completed &&
        this->iss.exec.current_insn == this->iss.csr.tdata2.value;
    if (trigger_match)
    {
        this->req_debug = true;
        this->req_debug_cause = 2;
    }

    if (this->step_state && step_completed && !this->iss.exec.debug_mode && !this->req_debug)
    {
        this->req_debug = true;
        this->req_debug_cause = 4;
    }

    if ((this->haltreq_level || this->haltreq_latch) && !this->req_debug &&
        !this->iss.exec.debug_mode)
    {
        this->req_debug = true;
    }

    if (this->req_debug && !this->iss.exec.debug_mode)
    {
        if (this->req_debug_cause == 3 && this->ebreak_enters_debug(insn))
        {
            this->req_debug_cause = 1;
        }
        return this->debug_enter();
    }

    return this->irq_check();
}

bool Cv32e40pIrq::ebreak_enters_debug(iss_insn_t *insn) const
{
    if (insn == NULL || !this->iss.csr.ebreak_m_mode_enters_debug())
    {
        return false;
    }
    // A compressed opcode carries the next parcel in its upper half.
    iss_opcode_t enc = insn->size == 2 ? (insn->opcode & 0xFFFF) : insn->opcode;
    return insn->size == 2 ? enc == 0x9002 : enc == 0x00100073;
}

iss_insn_t *Cv32e40pIrq::id_insn()
{
    iss_insn_t *insn = this->iss.insn_cache.get_insn(this->iss.exec.current_insn);
    return insn != NULL && this->iss.decode.is_decoded(insn) ? insn : NULL;
}

int Cv32e40pIrq::irq_check()
{
    if (this->iss.exec.debug_mode)
    {
        return 0;
    }

    // Masked while single-stepping unless dcsr.stepie=1.
    if (this->step_state && !((this->iss.csr.dcsr >> 11) & 1))
    {
        return 0;
    }

    // M-mode only, so a pending and enabled line is taken when mstatus.MIE is set.
    iss_reg_t pending = this->iss.csr.mie.value & this->iss.csr.mip.value & IRQ_MASK;
    if (!pending || !this->iss.csr.mstatus.mie)
    {
        return 0;
    }

    this->irq_take(pending);

    return 1;
}

/* The wake-up reads the pins, while the interrupt decisions read the
 * sampled levels (irq_wu_ctrl_o is irq_i & mie). A pending debug request
 * enters debug mode before the instruction after the WFI (DBG_TAKEN_IF). */
int Cv32e40pIrq::sleep_check()
{
    if (!this->iss.exec.wfi.get())
    {
        return 0;
    }
    if (this->debug_request_pending())
    {
        this->release_wfi();
        this->req_debug = true;
        return this->debug_enter();
    }
    if (this->irq_level & this->iss.csr.mie.value)
    {
        this->release_wfi();
        return 1;
    }
    return 0;
}

int Cv32e40pIrq::debug_enter()
{
    Cv32e40pCosimModel *cosim = this->iss.exec.cosim;
    cosim->boundary_begin(CV32E40P_COSIM_BOUNDARY_DEBUG_ENTER, this->req_debug_cause, 0,
        this->iss.exec.current_insn);
    this->debug_state_enter(this->req_debug_cause, this->iss.exec.current_insn);
    this->iss.exec.current_insn = this->debug_handler;
    cosim->csr(0x7B0, CV32E40P_COSIM_CSR_DEBUG_ENTRY);
    cosim->csr(0x7B1, CV32E40P_COSIM_CSR_DEBUG_ENTRY);
    cosim->boundary_end(this->iss.exec.current_insn);
    return 1;
}

iss_reg_t Cv32e40pIrq::ebreak_debug_entry(iss_reg_t pc)
{
    this->debug_state_enter(1, pc);
    Cv32e40pCosimModel *cosim = this->iss.exec.cosim;
    if (cosim->enabled())
    {
        cosim->trap(ISS_EXCEPT_BREAKPOINT, this->debug_handler, true);
        cosim->csr(0x7B0, CV32E40P_COSIM_CSR_DEBUG_ENTRY);
        cosim->csr(0x7B1, CV32E40P_COSIM_CSR_DEBUG_ENTRY);
    }
    return this->debug_handler;
}

void Cv32e40pIrq::debug_state_enter(int cause, iss_reg_t dpc)
{
    // Any debug entry closes the single-step window.
    this->step_state = 0;
    this->iss.exec.debug_mode = true;
    this->iss.csr.depc = dpc;
    this->iss.csr.dcsr = (this->iss.csr.dcsr & ~(0x7u << 6)) | ((iss_reg_t)(cause & 0x7) << 6);
    this->req_debug_cause = 3;
    this->debug_saved_irq_enable = this->irq_enable.get();
    this->irq_enable.set(0);
    this->req_debug = false;
}

void Cv32e40pIrq::irq_take(iss_reg_t pending)
{
    int irq = cv32e40p_irq_pick(pending);

    // mtvec is {base[31:8], 0, mode} (Cv32e40pCsr::mtvec_write_fixup).
    iss_reg_t base = this->iss.csr.mtvec.value & 0xFFFFFF00;
    iss_reg_t entry = (this->iss.csr.mtvec.value & 1) ? base + (irq << 2) : base;

    /* The interrupt branch of DECODE kills the instruction at step_pc and
     * ignores debug_single_step_i, so the stepped instruction is the first
     * one of the handler (cv32e40p_controller.sv). */
    if (this->step_state)
    {
        this->step_pc = entry;
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Handling IRQ (irq: %d, entry: 0x%lx)\n",
                    irq, entry);

    Cv32e40pCosimModel *cosim = this->iss.exec.cosim;
    cosim->boundary_begin(CV32E40P_COSIM_BOUNDARY_IRQ_TAKE, 0x80000000u | irq, irq,
        this->iss.exec.current_insn);

    this->iss.exec.interrupt_taken();
    this->iss.csr.mepc.value = this->iss.exec.current_insn;
    this->iss.csr.mstatus.mpie = this->iss.csr.mstatus.mie;
    this->iss.csr.mstatus.mie = 0;
    this->iss.csr.mstatus.mpp = this->iss.core.mode_get();
    this->iss.csr.mcause.value = (1ULL << (ISS_REG_WIDTH - 1)) | (unsigned int)irq;
    this->iss.exec.current_insn = entry;
    this->iss.core.mode_set(PRIV_M);
    this->irq_enable.set(0);

    cosim->csr(0x300, CV32E40P_COSIM_CSR_TAKE);
    cosim->csr(0x341, CV32E40P_COSIM_CSR_TAKE);
    cosim->csr(0x342, CV32E40P_COSIM_CSR_TAKE);
    cosim->boundary_end(entry);

    this->iss.timing.stall_insn_dependency_account(4);
}
