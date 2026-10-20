// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <string.h>
#include "cpu/iss_v2/include/iss.hpp"

SnitchFpSs::SnitchFpSs(Iss &iss)
: iss(iss), fsm_event(&iss, &SnitchFpSs::fsm_handler),
  pc_signal(iss, "fp_ss/pc", ISS_REG_WIDTH, vp::SignalCommon::ResetKind::HighZ),
  loop_signal(iss, "fp_ss/frep", 1, vp::SignalCommon::ResetKind::HighZ),
  queue_signal(iss, "fp_ss/queue", 8)
{
    iss.traces.new_trace("fp_ss", &this->trace, vp::DEBUG);
    iss.new_master_port("fp_data", &this->lsu_itf, (vp::Block *)this);
}

static void fpss_set_tag_latency(Iss &iss, const char *tag, int latency, bool divsqrt=false)
{
    std::vector<iss_decoder_item_t *> *items = iss.decode.get_insns_from_tag(tag);
    if (items == NULL) return;
    for (iss_decoder_item_t *item: *items)
    {
        if (item->u.insn.block_id != -1)
        {
            item->u.insn.resource_latency = latency;
            item->u.insn.resource_bandwidth = divsqrt;
        }
    }
}

void SnitchFpSs::start()
{
    // The decoder table is shared by all the cores
    if (__iss_isa_set.initialized) return;
    __iss_isa_set.initialized = true;

    // Every instruction with an FP register operand is offloaded. The kind
    // tells how it goes through the FP subsystem, see SnitchFpSsKind.
    for (iss_decoder_item_t *item: *this->iss.decode.get_insns_from_tag("all"))
    {
        iss_decoder_insn_t *desc = &item->u.insn;
        bool has_freg = false, has_freg_out = false, has_int_in = false, has_int_out = false;
        bool is_mem = false;

        desc->block_id = -1;

        for (int i = 0; i < desc->nb_args; i++)
        {
            iss_decoder_arg_t *arg = &desc->args[i];
            bool is_freg = (arg->flags & ISS_DECODER_ARG_FLAG_FREG) != 0;
            if (arg->type == ISS_DECODER_ARG_TYPE_IN_REG)
            {
                if (is_freg) has_freg = true;
                else has_int_in = true;
            }
            else if (arg->type == ISS_DECODER_ARG_TYPE_OUT_REG)
            {
                if (is_freg) has_freg = has_freg_out = true;
                else has_int_out = true;
            }
            else if (arg->type == ISS_DECODER_ARG_TYPE_INDIRECT_IMM ||
                arg->type == ISS_DECODER_ARG_TYPE_INDIRECT_REG)
            {
                is_mem = true;
            }
        }

        SnitchFpSsKind kind;
        if (desc->tags[ISA_TAG_FREP_ID])
        {
            kind = strcmp(desc->label, "frep.o") == 0 ? SNITCH_FPSS_FREP_O : SNITCH_FPSS_FREP_I;
        }
        else if (!has_freg)
        {
            // Integer memory accesses are checked against the consistency
            // address queue
            if (is_mem || strncmp(desc->label, "amo", 3) == 0 ||
                strncmp(desc->label, "lr.", 3) == 0 || strncmp(desc->label, "sc.", 3) == 0)
            {
                desc->stub_handler = &SnitchFpSs::caq_stub<false>;
                desc->stub_fast_handler = &SnitchFpSs::caq_stub<true>;
            }
            continue;
        }
        else if (is_mem)
        {
            kind = has_freg_out ? SNITCH_FPSS_LOAD : SNITCH_FPSS_STORE;
        }
        else if (has_int_out)
        {
            kind = SNITCH_FPSS_TO_INT;
        }
        else if (has_int_in)
        {
            kind = SNITCH_FPSS_FROM_INT;
        }
        else
        {
            kind = SNITCH_FPSS_COMPUTE;
        }

        desc->block_id = kind;
        desc->stub_handler = &SnitchFpSs::offload_stub<false>;
        desc->stub_fast_handler = &SnitchFpSs::offload_stub<true>;
        desc->resource_latency = CONFIG_GVSOC_ISS_SNITCH_FMA_LATENCY;
        desc->resource_bandwidth = 0;
    }

    // FPU latencies, from the instruction classes. Sign injections and
    // min / max are both conversions and non-computational ops, the latter
    // wins.
    fpss_set_tag_latency(this->iss, "fconv", CONFIG_GVSOC_ISS_SNITCH_CONV_LATENCY);
    fpss_set_tag_latency(this->iss, "sfconv", CONFIG_GVSOC_ISS_SNITCH_CONV_LATENCY);
    fpss_set_tag_latency(this->iss, "fnoncomp", CONFIG_GVSOC_ISS_SNITCH_NONCOMP_LATENCY);
    fpss_set_tag_latency(this->iss, "fmv", CONFIG_GVSOC_ISS_SNITCH_NONCOMP_LATENCY);
    fpss_set_tag_latency(this->iss, "fother", CONFIG_GVSOC_ISS_SNITCH_NONCOMP_LATENCY);
    fpss_set_tag_latency(this->iss, "sfother", CONFIG_GVSOC_ISS_SNITCH_NONCOMP_LATENCY);
    fpss_set_tag_latency(this->iss, "fdiv", CONFIG_GVSOC_ISS_SNITCH_DIVSQRT_LATENCY, true);
    fpss_set_tag_latency(this->iss, "sfdiv", CONFIG_GVSOC_ISS_SNITCH_DIVSQRT_LATENCY, true);
}

void SnitchFpSs::reset(bool active)
{
    if (active)
    {
        for (SnitchFpSsEntry &entry: this->queue)
        {
            for (iss_insn_t *insn: entry.stagger_insns) delete insn;
        }
        this->queue.clear();
        this->completions.clear();
        this->nb_insns = 0;
        this->nb_freps = 0;
        this->nb_inflight = 0;
        this->pending_bypass = false;
        this->loop_active = false;
        this->divsqrt_ready = 0;
        this->issue_id = 0;
        this->result_cycle = 0;
        this->current_addr = 0;
        this->current_entry = NULL;
        for (int i = 0; i < 32; i++)
        {
            this->fp_ready[i] = 0;
        }

        this->lsu_free = NULL;
        for (int i = 0; i < CONFIG_GVSOC_ISS_SNITCH_FP_LSU_NB_OUTSTANDING; i++)
        {
            SnitchFpLsuReq *req = &this->lsu_reqs[i];
            req->req.set_data((uint8_t *)&req->data);
            req->next = this->lsu_free;
            this->lsu_free = req;
        }
        this->lsu_denied = NULL;
        this->lsu_sync_reqs.clear();
        this->caq.clear();

        this->fsm_event.disable();
    }
}

bool SnitchFpSs::can_offload(SnitchFpSsKind kind)
{
    // A bypass instruction waits in the offload cut until the sequencer is
    // empty, nothing else can be offloaded meanwhile
    if (this->pending_bypass) return false;

    if (kind == SNITCH_FPSS_FREP_O || kind == SNITCH_FPSS_FREP_I)
    {
        return this->nb_freps < CONFIG_GVSOC_ISS_SNITCH_FREP_CONFIG_SIZE;
    }

    if ((kind == SNITCH_FPSS_LOAD || kind == SNITCH_FPSS_STORE) &&
        (int)this->caq.size() >= CONFIG_GVSOC_ISS_SNITCH_CAQ_DEPTH)
    {
        return false;
    }

    // The sequencer buffer plus the offload cut
    return this->nb_insns < CONFIG_GVSOC_ISS_SNITCH_SEQ_BUFFER_SIZE + 1;
}

void SnitchFpSs::caq_remove(iss_addr_t addr)
{
    for (size_t i = 0; i < this->caq.size(); i++)
    {
        if (this->caq[i] == addr)
        {
            this->caq.erase(this->caq.begin() + i);
            return;
        }
    }
}

template<bool fast>
iss_reg_t SnitchFpSs::caq_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    SnitchFpSs *_this = &iss->arch.fp_ss;

    if (unlikely(!_this->caq.empty()))
    {
        iss_addr_t addr = 0;
        bool found = false;
        for (int i = 0; i < insn->decoder_item->u.insn.nb_args; i++)
        {
            iss_insn_arg_t *arg = &insn->args[i];
            if (arg->type == ISS_DECODER_ARG_TYPE_INDIRECT_IMM)
            {
                addr = iss->regfile.get_reg(arg->u.indirect_imm.reg_index) + arg->u.indirect_imm.imm;
                found = true;
                break;
            }
        }
        if (!found)
        {
            // Atomics, the address is in rs1
            addr = REG_GET(0);
        }

        addr &= ~(iss_addr_t)7;
        for (iss_addr_t caq_addr: _this->caq)
        {
            if (caq_addr == addr)
            {
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Integer access waiting on CAQ (pc: 0x%lx, "
                    "addr: 0x%lx)\n", pc, addr);
                iss->exec.insn_stall();
                return pc;
            }
        }
    }

    return iss_stub_handler<fast>(insn)(iss, insn, pc);
}

template<bool fast>
iss_reg_t SnitchFpSs::offload_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    SnitchFpSs *_this = &iss->arch.fp_ss;
    SnitchFpSsKind kind = (SnitchFpSsKind)insn->decoder_item->u.insn.block_id;

    if (!_this->can_offload(kind))
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Offload stalled (pc: 0x%lx)\n", pc);
        iss->exec.insn_stall();
        return pc;
    }

    _this->queue.emplace_back();
    SnitchFpSsEntry &entry = _this->queue.back();
    entry.kind = kind;
    entry.latency = insn->resource_latency;
    entry.int_reg = -1;
    entry.int_value = 0;
    entry.caq_pending = false;
    entry.handler = iss_stub_handler<fast>(insn);
    entry.ready_cycle = iss->clock.get_cycles() + CONFIG_GVSOC_ISS_SNITCH_OFFLOAD_LATENCY;

    // The integer operands are captured now, the core may overwrite them
    // before the FP subsystem executes the instruction
    switch (kind)
    {
        case SNITCH_FPSS_LOAD:
        case SNITCH_FPSS_STORE:
            entry.int_value = REG_GET(0) + SIM_GET(0);
            _this->caq.push_back(entry.int_value & ~(iss_addr_t)7);
            entry.caq_pending = true;
            break;

        case SNITCH_FPSS_FREP_O:
        case SNITCH_FPSS_FREP_I:
            entry.int_value = REG_GET(0);
            break;

        case SNITCH_FPSS_FROM_INT:
            for (int i = 0; i < insn->decoder_item->u.insn.nb_args; i++)
            {
                iss_decoder_arg_t *arg = &insn->decoder_item->u.insn.args[i];
                if (arg->type == ISS_DECODER_ARG_TYPE_IN_REG &&
                    !(arg->flags & ISS_DECODER_ARG_FLAG_FREG))
                {
                    entry.int_reg = insn->args[i].u.reg.index;
                    entry.int_value = iss->regfile.get_reg(entry.int_reg);
                    break;
                }
            }
            break;

        default:
            break;
    }

    entry.entry = iss->exec.insn_hold(insn);
#ifdef VP_TRACE_ACTIVE
    if (!iss->trace.insn_trace.get_active())
    {
        entry.entry->trace = NULL;
    }
#else
    entry.entry->trace = NULL;
#endif

    _this->nb_inflight++;
    if (kind == SNITCH_FPSS_FREP_O || kind == SNITCH_FPSS_FREP_I)
    {
        _this->nb_freps++;
    }
    else
    {
        _this->nb_insns++;
        if (kind == SNITCH_FPSS_TO_INT || kind == SNITCH_FPSS_FROM_INT)
        {
            _this->pending_bypass = true;
        }
    }

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Offloaded instruction (pc: 0x%lx, kind: %d, "
        "nb_insns: %d, nb_freps: %d)\n", pc, kind, _this->nb_insns, _this->nb_freps);

    _this->fsm_event.enable();

    return iss_insn_next(iss, insn, pc);
}

void SnitchFpSs::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Iss *iss = (Iss *)__this;
    SnitchFpSs *_this = &iss->arch.fp_ss;
    int64_t cycle = iss->clock.get_cycles();

    // Instructions done
    for (size_t i = 0; i < _this->completions.size();)
    {
        if (_this->completions[i].first <= cycle)
        {
            InsnEntry *entry = _this->completions[i].second;
            _this->completions[i] = _this->completions.back();
            _this->completions.pop_back();
            _this->nb_inflight--;
            iss->exec.insn_terminate(entry);
        }
        else
        {
            i++;
        }
    }

    // FP LSU accesses answered synchronously, whose latency has elapsed
    for (size_t i = 0; i < _this->lsu_sync_reqs.size();)
    {
        SnitchFpLsuReq *req = _this->lsu_sync_reqs[i];
        if (req->timestamp <= cycle)
        {
            _this->lsu_sync_reqs[i] = _this->lsu_sync_reqs.back();
            _this->lsu_sync_reqs.pop_back();
            _this->lsu_end(req);
        }
        else
        {
            i++;
        }
    }

    _this->issue();

    _this->queue_signal = (int)_this->queue.size();

    _this->check_idle();
}

void SnitchFpSs::check_idle()
{
    if (this->queue.empty() && this->completions.empty() && this->lsu_sync_reqs.empty())
    {
        this->fsm_event.disable();
    }
}

void SnitchFpSs::complete_at(InsnEntry *entry, int64_t cycle)
{
    this->completions.emplace_back(cycle, entry);
}

void SnitchFpSs::loop_start(SnitchFpSsEntry &entry)
{
    iss_insn_t *insn = this->iss.exec.get_insn(entry.entry);

    this->loop_active = true;
    this->loop_inner = entry.kind == SNITCH_FPSS_FREP_I;
    this->loop_nb_insns = insn->uim[2] + 1;
    this->loop_nb_rpt = (uint64_t)entry.int_value + 1;
    this->loop_stagger_max = insn->uim[1];
    this->loop_stagger_mask = insn->uim[0];
    this->loop_insn = 0;
    this->loop_rpt = 0;

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "FREP start (inner: %d, nb_insns: %d, nb_rpt: %ld, "
        "stagger_max: %d, stagger_mask: 0x%x)\n", this->loop_inner, this->loop_nb_insns,
        this->loop_nb_rpt, this->loop_stagger_max, this->loop_stagger_mask);

    this->loop_signal = true;
}

void SnitchFpSs::pop_entry()
{
    SnitchFpSsEntry &entry = this->queue.front();
    if (entry.kind == SNITCH_FPSS_TO_INT || entry.kind == SNITCH_FPSS_FROM_INT)
    {
        this->pending_bypass = false;
    }
    for (iss_insn_t *insn: entry.stagger_insns) delete insn;
    this->queue.pop_front();
    this->nb_insns--;
}

bool SnitchFpSs::issue()
{
    int64_t cycle = this->iss.clock.get_cycles();

    while (1)
    {
        if (this->loop_active)
        {
            // The loop body is at the head of the queue, it may not be all
            // offloaded yet
            if (this->loop_insn >= (int)this->queue.size()) return false;

            int offset = 0;
            if (this->loop_stagger_max)
            {
                offset = this->loop_rpt % (this->loop_stagger_max + 1);
            }
            bool last = this->loop_rpt == this->loop_nb_rpt - 1;

            if (!this->issue_entry(this->loop_insn, offset, last)) return false;

            bool done;
            if (this->loop_inner)
            {
                this->loop_rpt++;
                if (this->loop_rpt == this->loop_nb_rpt)
                {
                    this->loop_rpt = 0;
                    this->loop_insn++;
                }
                done = this->loop_insn == this->loop_nb_insns;
            }
            else
            {
                this->loop_insn++;
                if (this->loop_insn == this->loop_nb_insns)
                {
                    this->loop_insn = 0;
                    this->loop_rpt++;
                }
                done = this->loop_rpt == this->loop_nb_rpt;
            }

            if (done)
            {
                this->trace.msg(vp::Trace::LEVEL_DEBUG, "FREP end\n");
                for (int i = 0; i < this->loop_nb_insns; i++)
                {
                    this->pop_entry();
                }
                this->loop_active = false;
                this->loop_signal = false;
            }
            return true;
        }

        if (this->queue.empty()) return false;

        SnitchFpSsEntry &head = this->queue.front();
        if (head.ready_cycle > cycle) return false;

        if (head.kind == SNITCH_FPSS_FREP_O || head.kind == SNITCH_FPSS_FREP_I)
        {
            // The loop configuration does not take an issue slot, the first
            // instruction of the body can go in the same cycle
            this->loop_start(head);
            this->complete_at(head.entry, cycle);
            this->queue.pop_front();
            this->nb_freps--;
            continue;
        }

        if (!this->issue_entry(0, 0, true)) return false;

        this->pop_entry();
        return true;
    }
}

iss_insn_t *SnitchFpSs::get_stagger_insn(SnitchFpSsEntry &entry, iss_insn_t *insn, int offset)
{
    if ((int)entry.stagger_insns.size() < offset)
    {
        entry.stagger_insns.resize(offset, NULL);
    }

    iss_insn_t *copy = entry.stagger_insns[offset - 1];
    if (copy == NULL)
    {
        // The staggered FP operands (mask bit 0: rd, 1: rs1, 2: rs2, 3: rs3)
        // get the offset added to their register index
        copy = new iss_insn_t(*insn);
        for (int i = 0; i < insn->decoder_item->u.insn.nb_args; i++)
        {
            iss_decoder_arg_t *darg = &insn->decoder_item->u.insn.args[i];
            if (!(darg->flags & ISS_DECODER_ARG_FLAG_FREG)) continue;

            int bit;
            if (darg->type == ISS_DECODER_ARG_TYPE_OUT_REG && darg->u.reg.id == 0)
            {
                bit = 0;
            }
            else if (darg->type == ISS_DECODER_ARG_TYPE_IN_REG && darg->u.reg.id < 3)
            {
                bit = darg->u.reg.id + 1;
            }
            else
            {
                continue;
            }

            if (!((this->loop_stagger_mask >> bit) & 1)) continue;

            int lid = copy->args[i].u.reg.index - ISS_NB_REGS;
            int reg = ISS_NB_REGS + ((lid + offset) & 0x1f);
            copy->args[i].u.reg.index = reg;
            if (darg->type == ISS_DECODER_ARG_TYPE_OUT_REG)
            {
                copy->out_regs[darg->u.reg.id] = reg;
            }
            else
            {
                copy->in_regs[darg->u.reg.id] = reg;
            }
        }
        entry.stagger_insns[offset - 1] = copy;
    }

    return copy;
}

bool SnitchFpSs::check_operands(iss_insn_t *insn, SnitchFpSsEntry &entry)
{
    int64_t cycle = this->iss.clock.get_cycles();
    bool ssr_enabled = this->iss.arch.ssr.is_enabled();
    uint32_t ssr_reads = 0, ssr_writes = 0;

    // FP scoreboard: sources written back (no forwarding), and no pending
    // write on the destination. Registers mapped to an SSR are not tracked,
    // their data comes from or goes to the SSR FIFO.
    for (int i = 0; i < insn->decoder_item->u.insn.nb_args; i++)
    {
        iss_decoder_arg_t *darg = &insn->decoder_item->u.insn.args[i];
        if (!(darg->flags & ISS_DECODER_ARG_FLAG_FREG)) continue;
        if (darg->type != ISS_DECODER_ARG_TYPE_IN_REG &&
            darg->type != ISS_DECODER_ARG_TYPE_OUT_REG) continue;

        int lid = insn->args[i].u.reg.index - ISS_NB_REGS;
        // The FP LSU writes back to the register file, never to an SSR
        bool is_load_dest = entry.kind == SNITCH_FPSS_LOAD &&
            darg->type == ISS_DECODER_ARG_TYPE_OUT_REG;
        if (ssr_enabled && lid < CONFIG_GVSOC_ISS_SNITCH_NB_SSRS && !is_load_dest)
        {
            if (darg->type == ISS_DECODER_ARG_TYPE_IN_REG) ssr_reads |= 1 << lid;
            else ssr_writes |= 1 << lid;
        }
        else if (this->fp_ready[lid] > cycle)
        {
            return false;
        }
    }

    for (int i = 0; i < CONFIG_GVSOC_ISS_SNITCH_NB_SSRS; i++)
    {
        if (((ssr_reads >> i) & 1) && !this->iss.arch.ssr.streamers[i]->can_read(cycle))
        {
            return false;
        }
        if (((ssr_writes >> i) & 1) && !this->iss.arch.ssr.streamers[i]->can_write())
        {
            return false;
        }
    }

    if (entry.kind == SNITCH_FPSS_LOAD || entry.kind == SNITCH_FPSS_STORE)
    {
        if (this->lsu_free == NULL || this->lsu_denied != NULL) return false;
    }

    if (insn->resource_bandwidth && this->divsqrt_ready > cycle) return false;

    return true;
}

bool SnitchFpSs::issue_entry(int index, int offset, bool last)
{
    int64_t cycle = this->iss.clock.get_cycles();
    SnitchFpSsEntry &entry = this->queue[index];

    if (entry.ready_cycle > cycle) return false;

    iss_insn_t *insn = this->iss.exec.get_insn(entry.entry);
    if (offset != 0)
    {
        insn = this->get_stagger_insn(entry, insn, offset);
    }

    if (!this->check_operands(insn, entry)) return false;

    int64_t done = cycle + entry.latency;

    this->issue_id++;
    this->result_cycle = done;
    this->current_addr = entry.int_value;
    this->current_entry = last ? entry.entry : NULL;
    this->current_caq_release = entry.caq_pending;
    entry.caq_pending = false;

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Issuing instruction (pc: 0x%lx, offset: %d, last: %d)\n",
        insn->addr, offset, last);
    this->pc_signal.set_and_release(insn->addr);

#ifdef VP_TRACE_ACTIVE
    // The instruction trace shows the operands at issue. The last execution
    // of an instruction is dumped by the core when it is terminated, the
    // others (FREP repetitions) right away.
    bool trace_active = this->iss.trace.insn_trace.get_active();
    bool trace_in_entry = last && entry.entry->trace != NULL;
    if (trace_active)
    {
        if (trace_in_entry)
        {
            iss_trace_save_args(&this->iss, insn, false, entry.entry->trace);
        }
        else
        {
            this->iss.trace.insn_trace_start(insn, insn->addr);
        }
    }
#endif

    if (entry.kind == SNITCH_FPSS_FROM_INT)
    {
        // Execute with the integer operand captured at offload
        uint64_t *regs = this->iss.regfile.dbt_regs();
        uint64_t saved = regs[entry.int_reg];
        regs[entry.int_reg] = entry.int_value;
        entry.handler(&this->iss, insn, insn->addr);
        regs[entry.int_reg] = saved;
    }
    else
    {
        entry.handler(&this->iss, insn, insn->addr);
    }

#ifdef VP_TRACE_ACTIVE
    if (trace_active && !trace_in_entry)
    {
        this->iss.trace.insn_trace_dump(insn, insn->addr);
    }
#endif

    // Result timing. The loads set it from their access.
    if (entry.kind != SNITCH_FPSS_LOAD)
    {
        bool ssr_enabled = this->iss.arch.ssr.is_enabled();
        for (int i = 0; i < insn->decoder_item->u.insn.nb_args; i++)
        {
            iss_decoder_arg_t *darg = &insn->decoder_item->u.insn.args[i];
            if (darg->type == ISS_DECODER_ARG_TYPE_OUT_REG && (darg->flags & ISS_DECODER_ARG_FLAG_FREG))
            {
                int lid = insn->args[i].u.reg.index - ISS_NB_REGS;
                if (!ssr_enabled || lid >= CONFIG_GVSOC_ISS_SNITCH_NB_SSRS)
                {
                    this->fp_ready[lid] = done + CONFIG_GVSOC_ISS_SNITCH_RESULT_LATENCY;
                }
            }
        }
    }

    if (insn->resource_bandwidth)
    {
        this->divsqrt_ready = done;
    }

    if (last)
    {
        switch (entry.kind)
        {
            // Terminated by the FP LSU
            case SNITCH_FPSS_LOAD:
            case SNITCH_FPSS_STORE:
                break;

            // The integer result goes back through the offload response
            case SNITCH_FPSS_TO_INT:
                this->complete_at(entry.entry, done + CONFIG_GVSOC_ISS_SNITCH_RESP_LATENCY);
                break;

            default:
                this->complete_at(entry.entry, done);
                break;
        }
    }

    return true;
}

void SnitchFpSs::lsu_send(SnitchFpLsuReq *req)
{
    vp::IoReqStatus status = this->lsu_itf.req(&req->req);

    if (status == vp::IO_REQ_DONE)
    {
        req->timestamp = this->iss.clock.get_cycles() + req->req.get_latency();
        this->lsu_sync_reqs.push_back(req);
        this->fsm_event.enable();
        if (req->req.get_opcode() == vp::IoReqOpcode::READ)
        {
            this->fp_ready[req->reg - ISS_NB_REGS] = req->timestamp +
                CONFIG_GVSOC_ISS_SNITCH_RESULT_LATENCY;
        }
    }
    else if (status == vp::IO_REQ_DENIED)
    {
        this->lsu_denied = req;
    }
}

void SnitchFpSs::load(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    SnitchFpLsuReq *req = this->lsu_free;
    this->lsu_free = req->next;

    this->trace.msg(vp::Trace::LEVEL_TRACE, "FP load (addr: 0x%lx, size: %d, reg: %d)\n",
        addr, size, reg);

    req->reg = reg;
    req->entry = this->current_entry;
    req->caq_addr = this->current_addr & ~(iss_addr_t)7;
    req->caq_release = this->current_caq_release;
    req->data = 0;

    vp::IoReq *io_req = &req->req;
    io_req->prepare();
    io_req->set_addr(addr);
    io_req->set_size(size);
    io_req->set_opcode(vp::IoReqOpcode::READ);
    io_req->set_data((uint8_t *)&req->data);
    io_req->set_resp_status(vp::IO_RESP_OK);

    // Pending until the data is back
    this->fp_ready[reg - ISS_NB_REGS] = INT64_MAX;

    this->lsu_send(req);
}

void SnitchFpSs::store(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    SnitchFpLsuReq *req = this->lsu_free;
    this->lsu_free = req->next;

    req->reg = reg;
    req->entry = this->current_entry;
    req->caq_addr = this->current_addr & ~(iss_addr_t)7;
    req->caq_release = this->current_caq_release;
    req->data = this->iss.regfile.get_freg(reg);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "FP store (addr: 0x%lx, size: %d, value: 0x%lx)\n",
        addr, size, req->data);

    vp::IoReq *io_req = &req->req;
    io_req->prepare();
    io_req->set_addr(addr);
    io_req->set_size(size);
    io_req->set_opcode(vp::IoReqOpcode::WRITE);
    io_req->set_data((uint8_t *)&req->data);
    io_req->set_resp_status(vp::IO_RESP_OK);

    this->lsu_send(req);
}

void SnitchFpSs::lsu_end(SnitchFpLsuReq *req)
{
    vp::IoReq *io_req = &req->req;

    if (io_req->get_resp_status() == vp::IO_RESP_INVALID)
    {
        this->trace.force_warning("Invalid FP access (addr: 0x%lx, size: %ld, is_write: %d)\n",
            io_req->get_addr(), io_req->get_size(), io_req->get_opcode() != vp::IoReqOpcode::READ);
    }

    if (io_req->get_opcode() == vp::IoReqOpcode::READ)
    {
        // Straight to the register file, an SSR mapping does not apply to
        // the FP LSU
        this->iss.regfile.Regfile::set_freg(req->reg, req->data);
        int lid = req->reg - ISS_NB_REGS;
        int64_t ready = this->iss.clock.get_cycles() + CONFIG_GVSOC_ISS_SNITCH_RESULT_LATENCY;
        if (this->fp_ready[lid] == INT64_MAX || this->fp_ready[lid] < ready)
        {
            this->fp_ready[lid] = ready;
        }
    }

    if (req->caq_release)
    {
        this->caq_remove(req->caq_addr);
    }

    if (req->entry)
    {
        this->nb_inflight--;
        this->iss.exec.insn_terminate(req->entry);
    }

    req->next = this->lsu_free;
    this->lsu_free = req;
}

void SnitchFpSs::lsu_retry(vp::Block *__this, vp::IoRetryChannel)
{
    SnitchFpSs *_this = (SnitchFpSs *)__this;
    SnitchFpLsuReq *req = _this->lsu_denied;

    if (req == NULL) return;

    // Re-issued synchronously, the accept window of the arbiters is only
    // open during this call
    _this->lsu_denied = NULL;
    _this->lsu_send(req);
}

vp::IoRespAck SnitchFpSs::lsu_response(vp::Block *__this, vp::IoReq *req)
{
    SnitchFpSs *_this = (SnitchFpSs *)__this;
    _this->lsu_end((SnitchFpLsuReq *)req);
    return vp::IO_RESP_ACCEPTED;
}
