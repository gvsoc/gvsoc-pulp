/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Germain Haugou, GreenWaves Technologies (germain.haugou@greenwaves-technologies.com)
 */

#include "cpu/iss/include/iss.hpp"
#include <string.h>


Iss::Iss(IssWrapper &top)
    : prefetcher(*this), exec(top, *this), insn_cache(*this), decode(*this), timing(*this), core(*this), irq(*this),
      gdbserver(*this), lsu(top, *this), dbgunit(*this), syscalls(top, *this), trace(*this), csr(*this),
      regfile(top, *this), exception(*this),
      memcheck(top, *this), top(top), fpu_lsu(top, *this)
#if defined(CONFIG_GVSOC_ISS_SEQUENCER)
      , sequencer(top, *this)
#endif

#if defined(CONFIG_GVSOC_ISS_SSR)
      , ssr(top, *this)
#endif
#if defined(CONFIG_GVSOC_ISS_USE_SPATZ)
      , vector(*this), vu(top, *this)
#endif
{
    this->csr.declare_csr(&this->csr_fmode, "fmode", 0x800);
    this->csr.declare_csr(&this->barrier,  "barrier",   0x7C2);
    this->barrier.register_callback(std::bind(&Iss::barrier_update, this, std::placeholders::_1,
        std::placeholders::_2));

    this->barrier_ack_itf.set_sync_meth(&Iss::barrier_sync);
    this->top.new_slave_port("barrier_ack", &this->barrier_ack_itf, (vp::Block *)this);

    this->top.new_master_port("barrier_req", &this->barrier_req_itf, (vp::Block *)this);
}


// This gets called when the barrier csr is accessed
bool Iss::barrier_update(bool is_write, iss_reg_t &value)
{
    if (!is_write)
    {
        // Since syncing the barrier can immediatly trigger it, we need
        // to flag that we are waiting fro the barrier.
        this->waiting_barrier = true;

        // Notify the global barrier
        if (this->barrier_req_itf.is_bound())
        {
            this->barrier_req_itf.sync(1);
        }

        // Now we have to check if the barrier was already reached and the barrier
        // sync function already called.
        if (this->waiting_barrier)
        {
            // If not, stall the core, this will get unstalled when the barrier sync is called
            this->exec.insn_stall();
        }
    }

    return false;
}


// This gets called when the barrier is reached
void Iss::barrier_sync(vp::Block *__this, bool value)
{
    Iss *_this = (Iss *)__this;

    // Clear the flag
    _this->waiting_barrier = false;

    // In case the barrier is reached as soon as we notify it, we are not yet stalled
    // and should not to anything
    if (_this->exec.is_stalled())
    {
#ifdef CONFIG_GVSOC_ISS_EXEC_WAKEUP_COUNTER
        if (_this->exec.wfi.get())
        {
            _this->exec.wfi.set(false);
            _this->exec.busy_enter();
            if (_this->exec.stall_cycles > (_this->top.clock.get_cycles() - _this->exec.wfi_start))
            {
                _this->exec.stall_cycles -= (_this->top.clock.get_cycles() - _this->exec.wfi_start);
            }
            else
            {
                _this->exec.stall_cycles = 0;
            }
        }
#endif
        _this->exec.stalled_dec();
        _this->exec.insn_terminate();
    }
}

void IssWrapper::start()
{
    vp_assert_always(this->iss.lsu.data.is_bound(), &this->trace, "Data master port is not connected\n");
    vp_assert_always(this->iss.prefetcher.fetch_itf.is_bound(), &this->trace, "Fetch master port is not connected\n");
    // vp_assert_always(this->irq_ack_itf.is_bound(), &this->trace, "IRQ ack master port is not connected\n");

    this->trace.msg("ISS start (fetch: %d, boot_addr: 0x%lx)\n",
        iss.exec.fetch_enable_reg.get(), get_js_config()->get_child_int("boot_addr"));

    this->iss.timing.background_power.leakage_power_start();
    this->iss.timing.background_power.dynamic_power_start();

    this->iss.lsu.start();
    this->iss.gdbserver.start();
}

void IssWrapper::stop()
{
    this->iss.insn_cache.stop();
    this->iss.gdbserver.stop();
}

void IssWrapper::reset(bool active)
{
    this->iss.prefetcher.reset(active);
    this->iss.csr.reset(active);
    this->iss.exec.reset(active);
    this->iss.core.reset(active);
    this->iss.irq.reset(active);
    this->iss.lsu.reset(active);
    this->iss.timing.reset(active);
    this->iss.trace.reset(active);
    this->iss.regfile.reset(active);
    this->iss.decode.reset(active);
    this->iss.gdbserver.reset(active);

#if defined(CONFIG_GVSOC_ISS_USE_SPATZ)
    this->iss.syscalls.reset(active);
    this->iss.vector.reset(active);
    this->iss.vu.reset(active);

    this->do_flush = false;
    this->insn_first = 0;
    this->insn_last = 0;
    this->nb_pending_insn = 0;
#endif
}

IssWrapper::IssWrapper(vp::ComponentConf &config)
    : vp::Component(config), iss(*this)
{
    this->iss.syscalls.build();
    this->iss.decode.build();
    this->iss.exec.build();
    this->iss.insn_cache.build();
    this->iss.dbgunit.build();
    this->iss.csr.build();
    this->iss.lsu.build();
    this->iss.irq.build();
    this->iss.trace.build();
    this->iss.timing.build();
    this->iss.gdbserver.build();
    this->iss.core.build();
    this->iss.exception.build();
    this->iss.prefetcher.build();

    this->traces.new_trace("wrapper", &this->trace, vp::DEBUG);

#if defined(CONFIG_GVSOC_ISS_USE_SPATZ)
    this->iss.vector.build();
    this->iss.vu.build();
    this->pending_insns.resize(8);
    for (int i=0; i<this->pending_insns.size(); i++)
    {
        this->pending_insns[i].id = i;
    }
#endif
}

#if defined(CONFIG_GVSOC_ISS_USE_SPATZ)
PendingInsn &IssWrapper::pending_insn_alloc()
{
    this->nb_pending_insn++;
    int insn_id = this->insn_last;
    this->insn_last++;
    if (this->insn_last == this->max_pending_insn)
    {
        this->insn_last = 0;
    }
    return this->pending_insns[insn_id];
}

PendingInsn &IssWrapper::pending_insn_enqueue(iss_insn_t *insn, iss_reg_t pc)
{
    PendingInsn &pending_insn = this->pending_insn_alloc();

    this->iss.exec.trace.msg(vp::Trace::LEVEL_TRACE, "Enqueue instruction (pc: 0x%lx)\n", pc);

    pending_insn.insn = insn;
    pending_insn.done = false;
    pending_insn.timestamp = this->clock.get_cycles() + 1;
    pending_insn.pc = pc;

    // this->fsm_event.enable();

    return pending_insn;
}

void IssWrapper::insn_commit(PendingInsn *pending_insn)
{
    iss_insn_t *insn = pending_insn->insn;

    this->iss.exec.trace.msg(vp::Trace::LEVEL_TRACE, "End of instruction (pc: 0x%lx)\n", pending_insn->pc);

    pending_insn->done = true;
    pending_insn->timestamp = this->clock.get_cycles() + 1;

    // Make output float registers available to unblock any scalar instructions stalled on them
    for (int i=0; i<insn->nb_out_reg; i++)
    {
        if ((insn->decoder_item->u.insn.args[i].u.reg.flags & ISS_DECODER_ARG_FLAG_VREG) == 0)
        {
            if ((insn->decoder_item->u.insn.args[i].u.reg.flags & ISS_DECODER_ARG_FLAG_FREG) != 0)
            {
                this->iss.sequencer.scoreboard_freg_timestamp[insn->out_regs[i]] = 0;
            }
            else
            {
                this->iss.regfile.scoreboard_reg_timestamp[insn->out_regs[i]] = 0;
            }
        }
    }

}

iss_reg_t IssWrapper::vector_insn_stub_handler(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    // We stall the instruction if ara queue is full
    if (iss->vu.queue_is_full())
    {
        iss->exec.trace.msg(vp::Trace::LEVEL_TRACE, "%s queue is full (pc: 0x%lx)\n",
            iss->vu.queue_is_full() ? "Ara" : "Core", pc);
        return pc;
    }

    // Account vector loads and stores to synchronize with snitch
    if (insn->decoder_item->u.insn.tags[ISA_TAG_VLOAD_ID])
    {
        iss->vu.nb_pending_vaccess++;
    }

    if (insn->decoder_item->u.insn.tags[ISA_TAG_VSTORE_ID])
    {
        iss->vu.nb_pending_vaccess++;
        iss->vu.nb_pending_vstore++;
    }

    // Only offload the instruction once all input registers are ready
    for (int i=0; i<insn->nb_in_reg; i++)
    {
        if ((insn->decoder_item->u.insn.args[insn->nb_out_reg + i].u.reg.flags & ISS_DECODER_ARG_FLAG_VREG) == 0)
        {
            if ((insn->decoder_item->u.insn.args[insn->nb_out_reg + i].u.reg.flags & ISS_DECODER_ARG_FLAG_FREG) == 0)
            {
                if (iss->regfile.scoreboard_reg_timestamp[insn->in_regs[i]] == -1)
                {
                    iss->exec.trace.msg(vp::Trace::LEVEL_TRACE, "Blocked due to int reg dependency (pc: 0x%lx, reg: %d)\n",
                        pc, insn->in_regs[i]);
                    return pc;
                }
            }
            else
            {
                int64_t cycles = iss->top.clock.get_cycles();
                if (iss->sequencer.scoreboard_freg_timestamp[insn->in_regs[i]] > cycles)
                {
                    iss->exec.trace.msg(vp::Trace::LEVEL_TRACE, "Blocked due to float reg dependency (pc: 0x%lx, reg: %d)\n",
                        pc, insn->in_regs[i]);
                    return pc;
                }
            }
        }
    }

    // Mark all output float registers as unavailable to prevent scalar instructions using them
    // as inputs to execute
    for (int i=0; i<insn->nb_out_reg; i++)
    {
        if ((insn->decoder_item->u.insn.args[i].u.reg.flags & ISS_DECODER_ARG_FLAG_VREG) == 0)
        {
            if ((insn->decoder_item->u.insn.args[i].u.reg.flags & ISS_DECODER_ARG_FLAG_FREG) != 0)
            {
                iss->sequencer.scoreboard_freg_timestamp[insn->out_regs[i]] = INT64_MAX;
            }
            else
            {
                iss->regfile.scoreboard_reg_timestamp[insn->out_regs[i]] = -1;
            }
        }
    }

    // Allocate a slot in cva6 queue and offload the instruction
    PendingInsn &pending_insn = iss->top.pending_insn_enqueue(insn, pc);

    iss->vu.insn_enqueue(&pending_insn);

    return iss_insn_next(iss, insn, pc);
}
#endif
