// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <string.h>
#include "cpu/iss_v2/include/iss.hpp"

SnitchV2::SnitchV2(Iss &iss)
: iss(iss)
, fp_ss(iss), ssr(iss)
, barrier_release_event(&iss, &SnitchV2::barrier_release_handler)
{
    this->iss.traces.new_trace("barrier", &this->barrier_trace, vp::DEBUG);

    this->iss.csr.declare_csr(&this->fmode, "fmode", 0x800);

    this->iss.csr.declare_csr(&this->barrier, "barrier", 0x7C2);
    this->barrier.register_callback(std::bind(&SnitchV2::barrier_update,
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // Scalar chaining mask, applied by the FP subsystem
    this->iss.csr.declare_csr(&this->sc, "sc", 0x7C3, 0, 0xFFFFFFFF);
    this->sc.register_callback(std::bind(&SnitchV2::sc_update,
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    this->iss.csr.declare_csr(&this->copift, "copift", 0x7C6, 0, 1);
    this->copift.register_callback(std::bind(&SnitchV2::copift_update,
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    this->iss.csr.declare_csr(&this->dump, "dump", 0x7C7);
    this->dump.register_callback(std::bind(&SnitchV2::dump_update,
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    this->barrier_ack_itf.set_sync_meth(&SnitchV2::barrier_sync);
    this->iss.new_slave_port("barrier_ack", &this->barrier_ack_itf, (vp::Block *)this);
    this->iss.new_master_port("barrier_req", &this->barrier_req_itf);
}

void SnitchV2::start()
{
    this->fp_ss.start();

    // The decoder table is shared by all the cores, so this is idempotent
    for (const char *name : {"csrrw", "csrrs", "csrrc", "csrrwi", "csrrsi", "csrrci"})
    {
        iss_decoder_item_t *item = this->iss.decode.get_insn(name);
        if (item != NULL)
        {
            item->u.insn.stub_handler = &SnitchV2::csr_stub<false>;
            item->u.insn.stub_fast_handler = &SnitchV2::csr_stub<true>;
        }
    }
}

void SnitchV2::reset(bool active)
{
    this->fp_ss.reset(active);
    this->ssr.reset(active);
    if (active)
    {
        this->barrier_waiting = false;
        this->barrier_stalled = false;
        this->copift_enabled = false;
        this->copift_redirect = false;
        this->in_fp_ss = false;
        this->f2i_value = 0;
        this->i2f_reserved = 0;
        this->i2f.clear();
        this->f2i.clear();
        this->barrier_release_event.cancel();
    }
}

void SnitchV2::decode_hook(iss_insn_t *insn)
{
    // The generic decoder builds the masks with 32-bit shifts, which fold
    // the FP registers onto the integer ones. Rebuild them on 64 bits, with
    // only the integer registers: the FP ones are tracked by the scoreboard of
    // the FP subsystem. The integer sources of an offloaded instruction must
    // be there to be captured at offload, and an integer destination stays
    // pending until the FP subsystem returns it.
    const int nb_regs = ISS_NB_REGS;
    uint64_t in_mask = 0;
    uint64_t out_mask = 0;

    for (int i = 0; i < insn->decoder_item->u.insn.nb_args; i++)
    {
        iss_insn_arg_t *arg = &insn->args[i];
        switch (arg->type)
        {
            case ISS_DECODER_ARG_TYPE_IN_REG:
                if (arg->u.reg.index < nb_regs)
                {
                    in_mask |= 1ULL << arg->u.reg.index;
                }
                break;

            case ISS_DECODER_ARG_TYPE_OUT_REG:
                if (arg->u.reg.index != 0 && arg->u.reg.index < nb_regs)
                {
                    out_mask |= 1ULL << arg->u.reg.index;
                }
                break;

            case ISS_DECODER_ARG_TYPE_INDIRECT_IMM:
                in_mask |= 1ULL << arg->u.indirect_imm.reg_index;
                break;

            case ISS_DECODER_ARG_TYPE_INDIRECT_REG:
                in_mask |= 1ULL << arg->u.indirect_reg.base_reg_index;
                in_mask |= 1ULL << arg->u.indirect_reg.offset_reg_index;
                break;

            default:
                break;
        }
    }

    insn->sb_reg_mask = in_mask | out_mask;
    insn->sb_in_reg_mask = in_mask;
    insn->sb_out_reg_mask = out_mask;

    // The integer instructions accessing x31 go through the COPIFT queues
    // when they are on
    if (insn->decoder_item->u.insn.block_id == -1 && ((in_mask | out_mask) >> 31) & 1)
    {
        insn->saved_handler = insn->handler;
        insn->handler = &SnitchV2::copift_stub;
        insn->fast_handler = &SnitchV2::copift_stub;
    }
}

iss_reg_t SnitchV2::copift_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    SnitchV2 *_this = &iss->arch;

    if (likely(!_this->copift_enabled))
    {
        return insn->saved_handler(iss, insn, pc);
    }

    bool reads = (insn->sb_in_reg_mask >> 31) & 1;
    bool writes = (insn->sb_out_reg_mask >> 31) & 1;
    int64_t cycle = iss->clock.get_cycles();

    if (reads && (_this->f2i.empty() || _this->f2i.front().ready > cycle))
    {
        _this->fp_ss.trace.msg(vp::Trace::LEVEL_TRACE, "Waiting for F2I (pc: 0x%lx)\n", pc);
        iss->exec.insn_stall();
        return pc;
    }
    if (writes && (int)_this->i2f.size() + _this->i2f_reserved >= SNITCH_COPIFT_QUEUE_DEPTH)
    {
        _this->fp_ss.trace.msg(vp::Trace::LEVEL_TRACE, "Waiting for I2F (pc: 0x%lx)\n", pc);
        iss->exec.insn_stall();
        return pc;
    }

    if (reads) _this->f2i_value = _this->f2i.front().value;
    if (writes) _this->i2f_reserved++;

    _this->copift_redirect = true;
    iss_reg_t next_pc = insn->saved_handler(iss, insn, pc);
    _this->copift_redirect = false;

    if (iss->exec.insn_is_stalled())
    {
        if (writes) _this->i2f_reserved--;
    }
    else if (reads)
    {
        _this->f2i.pop_front();
        _this->fp_ss.activate();
    }

    return next_pc;
}

template<bool fast>
iss_reg_t SnitchV2::csr_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t csr = UIM_GET(0);

    // The SSR enable and the scalar chaining mask are executed in order by the
    // FP subsystem, once its sequencer is empty. The FP status reads wait for
    // the FP instructions in flight, whose flags they report.
    if (((csr == 0x7C0 || csr == 0x7C3) && !iss->arch.fp_ss.is_drained()) ||
        ((csr == 0x001 || csr == 0x003) && !iss->arch.fp_ss.is_idle()))
    {
        iss->exec.insn_stall();
        return pc;
    }

    return iss_stub_handler<fast>(insn)(iss, insn, pc);
}

void SnitchV2::idle_hook(iss_insn_t *insn)
{
    // The L0 answers it inline
    vp::IoReq *req = &this->fetch_hint_req;
    req->prepare();
    req->set_addr(insn->addr + insn->size);
    req->set_size(0);
    req->set_is_write(false);
    req->set_data(NULL);
    this->iss.prefetch.fetch_itf.req(req);
}

bool SnitchV2::sc_update(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->fp_ss.sc_set_mask((uint32_t)value);
    }
    return true;
}

bool SnitchV2::copift_update(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->copift_enabled = value & 1;
        this->fp_ss.trace.msg(vp::Trace::LEVEL_DEBUG, "COPIFT queues %s\n",
            this->copift_enabled ? "enabled" : "disabled");
    }
    return true;
}

bool SnitchV2::dump_update(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        // Same print as the RTL core (simulation only), with the cycle
        // instead of the time
        uint32_t v = (uint32_t)value;
        float f;
        memcpy(&f, &v, sizeof(f));
        printf("[Dump Core %d] %ld 0x7c7 = 0x%08x, %d, %f\n", (int)this->iss.csr.mhartid,
            (long)this->iss.clock.get_cycles(), v, (int32_t)v, f);
        fflush(stdout);
    }
    else
    {
        value = 0;
    }
    return false;
}

bool SnitchV2::barrier_update(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (!is_write && this->barrier_req_itf.is_bound())
    {
        this->barrier_trace.msg(vp::Trace::LEVEL_DEBUG, "Entering barrier\n");

        // The last core to arrive gets its ack synchronously, from inside
        // the notification: flag the wait first so that it is seen.
        this->barrier_waiting = true;
        this->barrier_req_itf.sync(1);

        // The RTL core drops its fetch request while the barrier CSR stalls
        // it, and shows the address of the next instruction instead
        this->idle_hook(insn);

        if (!this->barrier_waiting)
        {
            // Last to arrive: the RTL barrier registers the arrivals and the
            // release, every core resumes 2 cycles after the last arrival, this
            // one as the others which were released at once
            this->iss.exec.stall_cycles_inc(1);
        }
        else
        {
            // Stall the core until barrier_sync releases it
            this->barrier_stalled = true;
            this->iss.exec.busy_exit();
            this->iss.exec.retain_inc();
        }
    }
    return false;
}

void SnitchV2::barrier_sync(vp::Block *__this, bool value)
{
    SnitchV2 *_this = (SnitchV2 *)__this;

    _this->barrier_trace.msg(vp::Trace::LEVEL_DEBUG, "Leaving barrier\n");

    _this->barrier_waiting = false;
    if (_this->barrier_stalled)
    {
        // The release of the RTL barrier is registered: the next instruction
        // goes 2 cycles after the last arrival
        _this->barrier_release_event.enqueue(1);
    }
}

void SnitchV2::barrier_release_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchV2 *_this = &((Iss *)__this)->arch;
    _this->barrier_stalled = false;
    _this->iss.exec.busy_enter();
    _this->iss.exec.retain_dec();
}
