// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

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
}

template<bool fast>
iss_reg_t SnitchV2::csr_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    iss_reg_t csr = UIM_GET(0);

    // The SSR enable is executed in order by the FP subsystem, once its
    // sequencer is empty. The FP status reads wait for the FP instructions
    // in flight, whose flags they report.
    if ((csr == 0x7C0 && !iss->arch.fp_ss.is_drained()) ||
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
