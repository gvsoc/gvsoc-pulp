// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cpu/iss_v2/include/iss.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>

// CSRs that the model adds to the records itself.
static constexpr uint32_t CSR_FFLAGS = 0x001, CSR_MSTATUS = 0x300, CSR_MEPC = 0x341,
    CSR_MCAUSE = 0x342, CSR_DCSR = 0x7B0, CSR_DPC = 0x7B1;

static std::string hex(uint32_t value)
{
    char text[9];
    snprintf(text, sizeof(text), "%03x", value);
    return text;
}

Cv32e40pCosimModel::Cv32e40pCosimModel(Iss &iss)
: iss(iss)
{
    memset(&this->info_, 0, sizeof(this->info_));
    this->info_.struct_size = sizeof(this->info_);
    this->info_.minor = CV32E40P_COSIM_MINOR;
    this->info_.pulp = CONFIG_GVSOC_ISS_CV32E40P_PULP;
    this->info_.fpu = CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA || CONFIG_GVSOC_ISS_CV32E40P_ZFINX;
    this->info_.zfinx = CONFIG_GVSOC_ISS_CV32E40P_ZFINX;
    this->info_.num_mhpmcounters = CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS;
    this->info_.irq_mask = Cv32e40pIrq::IRQ_MASK;
    this->info_.boot_addr = this->iss.exec.bootaddr_reg.get();
    this->info_.debug_handler = this->iss.irq.debug_handler;
    this->info_.debug_exception = this->iss.exception.debug_exception_handler_addr;
    memset(&this->config_, 0, sizeof(this->config_));
    memset(&this->current, 0, sizeof(this->current));
    memset(&this->boundary, 0, sizeof(this->boundary));
}

void Cv32e40pCosimModel::fail(const std::string &message)
{
    if (this->error.empty())
    {
        this->error = message;
    }
}

const Cv32e40pCosimInfo *Cv32e40pCosimModel::info() const
{
    return &this->info_;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::configure(const Cv32e40pCosimConfig *config)
{
    if (this->configured || config == NULL || config->struct_size < sizeof(uint32_t) * 2 ||
        config->n_regions > CV32E40P_COSIM_MAX_REGIONS)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    for (uint32_t i = 0; i < config->n_regions; i++)
    {
        if (config->region[i].kind > CV32E40P_COSIM_REGION_STORE_ONLY ||
            config->region[i].length == 0)
        {
            return CV32E40P_COSIM_REJECTED;
        }
    }
    memcpy(&this->config_, config, std::min((size_t)config->struct_size, sizeof(this->config_)));
    this->configured = true;
    this->info_.clock_period_ps = this->iss.clock.get_period();
    // The records are built by the full handlers (Cv32e40pExec::can_switch_to_fast_mode).
    this->iss.exec.switch_to_full_mode();
    return CV32E40P_COSIM_OK;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::arm(uint64_t sequence)
{
    if (!this->configured || sequence < this->armed)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    this->armed = sequence;
    if (this->iss.exec.wfi.get())
    {
        this->consume();
    }
    return this->status();
}

Cv32e40pCosimStatus Cv32e40pCosimModel::status() const
{
    if (!this->error.empty())
    {
        return CV32E40P_COSIM_ERROR;
    }
    if (!this->events.empty())
    {
        const Cv32e40pCosimEvent &next = this->events.front();
        if (this->external_pending && next.type == CV32E40P_COSIM_EVENT_COMMIT &&
            next.u.commit.sequence == this->external.sequence)
        {
            return CV32E40P_COSIM_EXTERNAL_LOAD;
        }
        return CV32E40P_COSIM_EVENT;
    }
    if (this->iss.exec.wfi.get())
    {
        return CV32E40P_COSIM_SLEEPING;
    }
    return CV32E40P_COSIM_RUNNING;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::poll(Cv32e40pCosimEvent *event)
{
    if (!this->configured || event == NULL || event->struct_size < sizeof(uint32_t) * 2)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    Cv32e40pCosimStatus status = this->status();
    if (status != CV32E40P_COSIM_EVENT)
    {
        return status;
    }
    Cv32e40pCosimEvent &next = this->events.front();
    uint32_t size = event->struct_size;
    memcpy(event, &next, std::min((size_t)size, sizeof(next)));
    event->struct_size = size;
    this->events.pop_front();
    return CV32E40P_COSIM_EVENT;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::input(const Cv32e40pCosimInput *input)
{
    if (!this->configured || input == NULL || input->input_sequence != this->input_sequence + 1)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    this->input_sequence = input->input_sequence;
    this->pending.push_back(
        { Pending::INPUT, input->irq_level, input->debug_req != 0, 0, 0, 0, 0 });
    return this->status();
}

Cv32e40pCosimStatus Cv32e40pCosimModel::sample(const Cv32e40pCosimSample *sample)
{
    if (!this->configured || sample == NULL ||
        sample->sample_sequence != this->sample_sequence + 1 ||
        sample->domain > CV32E40P_COSIM_DOMAIN_DEBUG)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    this->sample_sequence = sample->sample_sequence;
    this->pending.push_back({ Pending::SAMPLE, 0, 0, sample->domain, 0, 0, 0 });
    return this->status();
}

Cv32e40pCosimStatus Cv32e40pCosimModel::opportunity(const Cv32e40pCosimOpportunity *opportunity)
{
    if (!this->configured || opportunity == NULL ||
        opportunity->opportunity_id != this->opportunity_id + 1 ||
        opportunity->kind > CV32E40P_COSIM_OPP_BOOT)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    if (opportunity->ordinal <= this->last_sequence)
    {
        this->fail("decision point for instruction " + std::to_string(opportunity->ordinal) +
            ", which has already executed");
        return CV32E40P_COSIM_REJECTED;
    }
    this->opportunity_id = opportunity->opportunity_id;
    this->pending.push_back({ Pending::DECISION, 0, 0, 0, opportunity->kind, opportunity->ordinal,
        opportunity->opportunity_id });
    // A sleeping core does not reach the next dispatch, where the decision points are applied.
    if (this->iss.exec.wfi.get())
    {
        this->consume();
    }
    return this->status();
}

bool Cv32e40pCosimModel::consume()
{
    const bool sleeping = this->iss.exec.wfi.get();
    const uint64_t next = this->last_sequence + 1;
    bool moved = false;
    while (!this->pending.empty() && this->error.empty())
    {
        const Pending p = this->pending.front();
        if (p.type == Pending::DECISION)
        {
            if (p.ordinal > next)
            {
                break;
            }
            if (p.ordinal < next)
            {
                this->fail("decision point for instruction " + std::to_string(p.ordinal) +
                    " reported after it executed");
                break;
            }
            if (sleeping && p.kind != CV32E40P_COSIM_OPP_SLEEP)
            {
                this->fail("the RTL left the sleep before instruction " + std::to_string(next) +
                    ", the core did not wake up");
                break;
            }
        }
        this->pending.pop_front();
        switch (p.type)
        {
            case Pending::INPUT:
                this->iss.irq.input_set(p.irq_level, p.debug_req);
                break;
            case Pending::SAMPLE:
                this->iss.irq.input_sample(p.domain);
                break;
            case Pending::DECISION:
                this->decided = p.ordinal;
                this->decided_id = p.opportunity_id;
                moved |= this->iss.irq.decide(p.kind) != 0;
                break;
        }
        if (sleeping && !this->iss.exec.wfi.get())
        {
            // The core woke up. The next decision points are applied at the dispatch.
            break;
        }
    }
    return moved;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::volatile_csr(uint32_t address)
{
    if (!this->configured || address >= this->volatile_csrs.size())
    {
        return CV32E40P_COSIM_REJECTED;
    }
    this->volatile_csrs[address] = true;
    return this->status();
}

Cv32e40pCosimStatus Cv32e40pCosimModel::volatile_read(const Cv32e40pCosimVolatileRead *read)
{
    if (!this->configured || read == NULL || read->address >= this->volatile_csrs.size() ||
        !this->volatile_csrs[read->address])
    {
        return CV32E40P_COSIM_REJECTED;
    }
    if (read->sequence <= this->last_sequence ||
        (!this->volatile_reads.empty() && read->sequence < this->volatile_reads.back().sequence))
    {
        this->fail("value of CSR 0x" + hex(read->address) + " for instruction " +
            std::to_string(read->sequence) + ", which has already executed");
        return CV32E40P_COSIM_REJECTED;
    }
    this->volatile_reads.push_back({ read->sequence, read->address, read->value, false });
    return this->status();
}

void Cv32e40pCosimModel::csr_read(iss_insn_t *insn, uint32_t address, iss_reg_t &value)
{
    if (!this->open || address >= this->volatile_csrs.size() || !this->volatile_csrs[address])
    {
        return;
    }
    const uint64_t sequence = this->last_sequence + 1;
    for (VolatileRead &read : this->volatile_reads)
    {
        if (read.sequence == sequence && read.address == address)
        {
            value = read.value;
            read.used = true;
            return;
        }
    }
    // A read into x0 is not observable, so it needs no value. The decoder
    // maps x0 to ISS_DUMMY_REG.
    if (insn->out_regs[0] != ISS_DUMMY_REG)
    {
        this->fail("instruction " + std::to_string(sequence) + " reads the volatile CSR 0x" +
            hex(address) + " without the value the RTL read");
    }
}

Cv32e40pCosimStatus Cv32e40pCosimModel::region(const Cv32e40pCosimRegion *region)
{
    if (!this->configured || this->last_sequence != 0 || this->open || region == NULL ||
        region->kind > CV32E40P_COSIM_REGION_STORE_ONLY || region->length == 0 ||
        this->config_.n_regions == CV32E40P_COSIM_MAX_REGIONS)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    this->config_.region[this->config_.n_regions++] = *region;
    return CV32E40P_COSIM_OK;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::external_load_query(Cv32e40pCosimExternalLoad *load)
{
    if (load == NULL || load->struct_size < sizeof(Cv32e40pCosimExternalLoad) ||
        this->status() != CV32E40P_COSIM_EXTERNAL_LOAD)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    load->sequence = this->external.sequence;
    load->address = this->external.address;
    load->size = this->external.size;
    load->reg = this->external.reg;
    return CV32E40P_COSIM_OK;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::external_load_response(uint64_t sequence, uint32_t data)
{
    if (this->status() != CV32E40P_COSIM_EXTERNAL_LOAD || sequence != this->external.sequence)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    // The core stops after the armed instruction, so no later instruction
    // has read the destination register yet.
    const uint32_t size = this->external.size, reg = this->external.reg;
    const uint32_t mask = size >= 4 ? 0xFFFFFFFFu : ((1u << (8 * size)) - 1);
    uint32_t value = data & mask;
    if (this->external.is_signed && size < 4 && ((value >> (8 * size - 1)) & 1))
    {
        value |= ~mask;
    }
    Cv32e40pCosimCommit &commit = this->events.front().u.commit;
    if (reg != 0)
    {
        // The record is closed, so set_reg() does not add this write to it.
        // The value is replaced in the record below.
        this->iss.regfile.set_reg(reg, value);
        bool is_fpr = reg >= 32;
        Cv32e40pCosimRegWrite *writes = is_fpr ? commit.fpr : commit.gpr;
        uint32_t count = is_fpr ? commit.n_fpr : commit.n_gpr;
        for (uint32_t i = 0; i < count; i++)
        {
            if (writes[i].index == reg % 32)
            {
                writes[i].value = value;
            }
        }
    }
    for (uint32_t i = 0; i < commit.n_mem; i++)
    {
        if (!commit.mem[i].is_store && commit.mem[i].address == this->external.address)
        {
            commit.mem[i].data = data & mask;
        }
    }
    this->external_pending = false;
    return CV32E40P_COSIM_OK;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::read_gpr(uint32_t index, uint32_t *value) const
{
    if (index >= 32 || value == NULL)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    *value = index == 0 ? 0 : (uint32_t)this->iss.regfile.get_reg(index);
    return CV32E40P_COSIM_OK;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::read_fpr(uint32_t index, uint32_t *value) const
{
    if (!this->info_.fpu || this->info_.zfinx || index >= 32 || value == NULL)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    // In the unified register file, f<index> is register 32 + index.
    *value = (uint32_t)this->iss.regfile.get_reg(32 + index);
    return CV32E40P_COSIM_OK;
}

uint32_t Cv32e40pCosimModel::csr_value(uint32_t address) const
{
    /* The read callbacks change no architectural state. An HPM counter read
     * only adds the elapsed cycles to the counter (hpm_cycle_fold). */
    CsrAbtractReg *reg = this->iss.csr.get_csr(address);
    iss_reg_t value = 0;
    if (reg != NULL)
    {
        reg->access(NULL, false, value);
    }
    return (uint32_t)value;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::read_csr(uint32_t address, uint32_t *value) const
{
    if (value == NULL || this->iss.csr.get_csr(address) == NULL)
    {
        return CV32E40P_COSIM_REJECTED;
    }
    *value = this->csr_value(address);
    return CV32E40P_COSIM_OK;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::read_memory(uint32_t address, uint32_t size,
    uint8_t *data) const
{
    // Not implemented. The checker compares memory through the store records.
    return CV32E40P_COSIM_REJECTED;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::snapshot(Cv32e40pCosimSnapshot *snapshot) const
{
    if (snapshot == NULL || snapshot->struct_size < sizeof(*snapshot))
    {
        return CV32E40P_COSIM_REJECTED;
    }
    snapshot->next_pc = this->iss.exec.current_insn;
    snapshot->debug_mode = this->iss.exec.debug_mode;
    snapshot->sleeping = this->iss.exec.wfi.get();
    snapshot->sequence = this->last_sequence;
    snapshot->input_sequence = this->input_sequence;
    snapshot->sample_sequence = this->sample_sequence;
    snapshot->opportunity_id = this->opportunity_id;
    snapshot->time_ps = this->iss.time.get_time();
    strncpy(snapshot->error, this->error.c_str(), sizeof(snapshot->error) - 1);
    snapshot->error[sizeof(snapshot->error) - 1] = 0;
    return CV32E40P_COSIM_OK;
}

Cv32e40pCosimStatus Cv32e40pCosimModel::finish(Cv32e40pCosimSnapshot *snapshot)
{
    Cv32e40pCosimStatus status = this->snapshot(snapshot);
    this->configured = false;
    return status;
}

void Cv32e40pCosimModel::add_csr(Cv32e40pCosimCsrWrite *list, uint32_t &count, uint32_t max,
    uint32_t address, uint32_t origin)
{
    // The CSRs stay in the order of their first write, and a later write
    // replaces the value.
    uint32_t value = this->csr_value(address);
    for (uint32_t i = 0; i < count; i++)
    {
        if (list[i].address == address)
        {
            list[i].value = value;
            list[i].origin = origin;
            return;
        }
    }
    if (count == max)
    {
        this->current.flags |= CV32E40P_COSIM_REC_OVERFLOW;
        this->fail("too many CSR writes in one record");
        return;
    }
    list[count++] = { address, value, 0xFFFFFFFF, origin };
}

bool Cv32e40pCosimModel::dispatch(iss_insn_t *insn)
{
    if (!this->configured)
    {
        return false;
    }
    if (!this->error.empty() || this->last_sequence + 1 > this->armed)
    {
        return true;
    }
    if (this->open)
    {
        // The instruction is dispatched again after a stall. Its record stays open.
        return false;
    }
    if (this->consume())
    {
        // An interrupt or a debug entry moved the PC, so fetch again.
        return true;
    }
    if (this->decided != this->last_sequence + 1)
    {
        this->fail("no decision point reported for instruction " +
            std::to_string(this->last_sequence + 1));
        return true;
    }
    memset(&this->current, 0, sizeof(this->current));
    this->current.struct_size = sizeof(this->current);
    this->current.pc = insn->addr;
    this->current.insn_len = insn->size;
    this->current.insn = insn->size == 2 ? (insn->opcode & 0xFFFF) : insn->opcode;
    this->debug_before = this->iss.exec.debug_mode;
    this->fflags_before = this->iss.csr.fcsr.fflags;
    this->mstatus_before = this->csr_value(CSR_MSTATUS);
    this->open = true;
    return false;
}

void Cv32e40pCosimModel::retry(iss_insn_t *insn)
{
    if (!this->open)
    {
        return;
    }
    // The next attempt reports its external load again.
    this->external_pending = false;
    if (this->iss.exec.has_exception)
    {
        // The instruction raised illegal-instruction. It ends here with a
        // trapped record.
        this->retire(insn);
        return;
    }
    // The next attempt reports the effects of the stalled one again, so drop them.
    this->current.n_gpr = this->current.n_fpr = this->current.n_csr = this->current.n_mem = 0;
    this->current.flags = 0;
}

void Cv32e40pCosimModel::gpr(int reg, uint32_t value)
{
    if (!this->open || reg <= 0 || reg >= 32)
    {
        return;
    }
    for (uint32_t i = 0; i < this->current.n_gpr; i++)
    {
        if (this->current.gpr[i].index == (uint32_t)reg)
        {
            this->current.gpr[i].value = value;
            return;
        }
    }
    if (this->current.n_gpr == CV32E40P_COSIM_MAX_GPR)
    {
        this->current.flags |= CV32E40P_COSIM_REC_OVERFLOW;
        this->fail("too many GPR writes in one record");
        return;
    }
    this->current.gpr[this->current.n_gpr++] = { (uint32_t)reg, value };
}

void Cv32e40pCosimModel::fpr(int reg, uint32_t value)
{
    if (!this->open || reg < 0 || reg >= 32)
    {
        return;
    }
    for (uint32_t i = 0; i < this->current.n_fpr; i++)
    {
        if (this->current.fpr[i].index == (uint32_t)reg)
        {
            this->current.fpr[i].value = value;
            return;
        }
    }
    if (this->current.n_fpr == CV32E40P_COSIM_MAX_FPR)
    {
        this->current.flags |= CV32E40P_COSIM_REC_OVERFLOW;
        this->fail("too many FPR writes in one record");
        return;
    }
    this->current.fpr[this->current.n_fpr++] = { (uint32_t)reg, value };
}

void Cv32e40pCosimModel::csr(uint32_t address, uint32_t origin)
{
    if (this->in_boundary)
    {
        this->add_csr(this->boundary.csr, this->boundary.n_csr, CV32E40P_COSIM_MAX_BOUNDARY_CSR,
            address, origin);
    }
    else if (this->open)
    {
        this->add_csr(this->current.csr, this->current.n_csr, CV32E40P_COSIM_MAX_CSR,
            address, origin);
    }
}

void Cv32e40pCosimModel::trap(uint32_t cause, uint32_t target, bool debug_mode_trap)
{
    if (!this->open)
    {
        return;
    }
    this->current.flags |= CV32E40P_COSIM_REC_TRAPPED;
    this->current.trap_cause = cause;
    this->current.trap_target = target;
    if (!debug_mode_trap)
    {
        this->csr(CSR_MSTATUS, CV32E40P_COSIM_CSR_TRAP);
        this->csr(CSR_MEPC, CV32E40P_COSIM_CSR_TRAP);
        this->csr(CSR_MCAUSE, CV32E40P_COSIM_CSR_TRAP);
    }
}

void Cv32e40pCosimModel::mem(bool is_store, uint32_t address, uint32_t size, uint32_t data)
{
    if (!this->open)
    {
        return;
    }
    if (this->current.n_mem == CV32E40P_COSIM_MAX_MEM)
    {
        this->current.flags |= CV32E40P_COSIM_REC_OVERFLOW;
        this->fail("too many memory operations in one record");
        return;
    }
    uint32_t byte_enable = ((1u << size) - 1) << (address & 3);
    this->current.mem[this->current.n_mem++] =
        { this->last_sequence + 1, is_store, address, size, byte_enable, data };
}

void Cv32e40pCosimModel::load(uint32_t address, uint32_t size, int reg, bool is_signed)
{
    if (!this->open)
    {
        return;
    }
    for (uint32_t i = 0; i < this->config_.n_regions; i++)
    {
        const Cv32e40pCosimRegion &region = this->config_.region[i];
        if (region.kind == CV32E40P_COSIM_REGION_RAM ||
            address + size <= region.base || address >= region.base + region.length)
        {
            continue;
        }
        if (address < region.base || address + size > region.base + region.length)
        {
            this->fail("load 0x" + hex(address) + " of " + std::to_string(size) +
                " bytes crosses the boundary of an external region");
            return;
        }
        // A load that stalls is reported again when it is retried, and the new
        // report replaces the old one. A destination beyond the F registers is
        // x0 (ISS_DUMMY_REG), which receives no value.
        this->external = { this->last_sequence + 1, address, size,
            reg < ISS_NB_REGS + ISS_NB_FREGS ? (uint32_t)reg : 0, is_signed };
        this->external_pending = true;
        return;
    }
}

void Cv32e40pCosimModel::publish(const Cv32e40pCosimCommit &commit)
{
    Cv32e40pCosimEvent event;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = CV32E40P_COSIM_EVENT_COMMIT;
    event.u.commit = commit;
    this->events.push_back(event);
}

void Cv32e40pCosimModel::push(const Cv32e40pCosimEvent &event)
{
    // While some records are held back, a new event is queued after them,
    // because they come first in program order.
    if (this->held.empty())
    {
        this->events.push_back(event);
    }
    else
    {
        this->held.push_back(event);
    }
}

void Cv32e40pCosimModel::retire(iss_insn_t *insn)
{
    if (!this->open)
    {
        return;
    }

    Cv32e40pCosimCommit &commit = this->current;
    commit.sequence = ++this->last_sequence;
    if (this->external_pending && this->external.sequence < commit.sequence)
    {
        this->fail("instruction " + std::to_string(commit.sequence) +
            " executed before the data of the external load of instruction " +
            std::to_string(this->external.sequence));
    }
    while (!this->volatile_reads.empty() &&
        this->volatile_reads.front().sequence <= commit.sequence)
    {
        if (!this->volatile_reads.front().used)
        {
            this->fail("instruction " + std::to_string(this->volatile_reads.front().sequence) +
                " does not read the volatile CSR 0x" + hex(this->volatile_reads.front().address));
        }
        this->volatile_reads.pop_front();
    }
    commit.cycle = this->iss.clock.get_cycles();
    commit.minstret = ((uint64_t)this->iss.csr.minstreth.value << 32)
        | (uint32_t)this->iss.csr.minstret.value;
    commit.next_pc = this->iss.exec.has_exception ? this->iss.exec.exception_pc
                                                  : this->iss.exec.current_insn;
    if (this->debug_before)
    {
        commit.flags |= CV32E40P_COSIM_REC_DEBUG_BEFORE;
    }
    if (this->iss.exec.debug_mode)
    {
        commit.flags |= CV32E40P_COSIM_REC_DEBUG_AFTER;
    }
    if (!(commit.flags & CV32E40P_COSIM_REC_TRAPPED))
    {
        if (this->iss.csr.fcsr.fflags != this->fflags_before)
        {
            this->csr(CSR_FFLAGS, CV32E40P_COSIM_CSR_FP_STATE);
        }
        if (this->csr_value(CSR_MSTATUS) != this->mstatus_before)
        {
            bool written = false;
            for (uint32_t i = 0; i < commit.n_csr; i++)
            {
                written |= commit.csr[i].address == CSR_MSTATUS;
            }
            if (!written)
            {
                this->csr(CSR_MSTATUS, CV32E40P_COSIM_CSR_FP_STATE);
            }
        }
    }

    this->open = false;

    if (this->iss.exec.wfi.get())
    {
        // The RTL retires a WFI before it sleeps, so publish its record now.
        this->wfi_drains++;
        this->publish(commit);
    }
    else if (this->iss.exec.queue_head != NULL)
    {
        // The instruction is held in the commit FIFO, or it completed behind a
        // held one. Its record waits for drain().
        Cv32e40pCosimEvent event;
        memset(&event, 0, sizeof(event));
        event.struct_size = sizeof(event);
        event.type = CV32E40P_COSIM_EVENT_COMMIT;
        event.u.commit = commit;
        this->held.push_back(event);
    }
    else
    {
        this->publish(commit);
    }
}

void Cv32e40pCosimModel::drain()
{
    if (!this->configured)
    {
        return;
    }
    if (this->wfi_drains > 0)
    {
        // The held WFI leaves the commit FIFO when the core wakes up. Its
        // record is already published, so only the wake-up is added.
        this->wfi_drains--;
        this->wfi_wake();
        return;
    }
    if (this->held.empty() || this->held.front().type != CV32E40P_COSIM_EVENT_COMMIT)
    {
        this->fail("commit FIFO drain without a held record");
        return;
    }
    this->events.push_back(this->held.front());
    this->held.pop_front();
    while (!this->held.empty() && this->held.front().type == CV32E40P_COSIM_EVENT_BOUNDARY)
    {
        this->events.push_back(this->held.front());
        this->held.pop_front();
    }
}

void Cv32e40pCosimModel::boundary_begin(uint32_t kind, uint32_t cause, uint32_t irq_id, uint32_t pc)
{
    if (!this->configured)
    {
        return;
    }
    memset(&this->boundary, 0, sizeof(this->boundary));
    this->boundary.struct_size = sizeof(this->boundary);
    this->boundary.kind = kind;
    this->boundary.opportunity_id = this->decided_id;
    this->boundary.after_sequence = this->last_sequence;
    this->boundary.cause = cause;
    this->boundary.irq_id = irq_id;
    this->boundary.pc = pc;
    this->in_boundary = true;
}

void Cv32e40pCosimModel::boundary_end(uint32_t next_pc)
{
    if (!this->in_boundary)
    {
        return;
    }
    this->in_boundary = false;
    this->boundary.next_pc = next_pc;
    Cv32e40pCosimEvent event;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.type = CV32E40P_COSIM_EVENT_BOUNDARY;
    event.u.boundary = this->boundary;
    this->push(event);
}

void Cv32e40pCosimModel::wfi_wake()
{
    this->boundary_begin(CV32E40P_COSIM_BOUNDARY_WAKE, 0, 0, this->iss.exec.current_insn);
    this->boundary_end(this->iss.exec.current_insn);
}

extern "C" Cv32e40pCosim *cv32e40p_cosim_acquire_v1(void *component, uint32_t major,
    uint32_t *minor)
{
    if (component == NULL || major != CV32E40P_COSIM_MAJOR)
    {
        return NULL;
    }
    // Every iss_v2 core defines a class named Iss, so check a property that
    // only the CV32E40P recipe sets before the cast.
    vp::Component *comp = (vp::Component *)component;
    js::Config *config = comp->get_js_config()->get("cv32e40p_cosim");
    if (config == NULL || !config->get_bool())
    {
        return NULL;
    }
    Iss *iss = static_cast<Iss *>(comp);
    if (minor != NULL)
    {
        *minor = CV32E40P_COSIM_MINOR;
    }
    return iss->exec.cosim;
}

void Cv32e40pExec::start()
{
    this->ExecInOrder::start();
    // Created here, once every slot is built, because the model reads their
    // configuration.
    this->cosim = new Cv32e40pCosimModel(this->iss);
}
