/*
* Copyright (C) 2026 ETH Zurich, University of Bologna, and Fondazione Chips-IT
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
*
* Authors:  Alessandro Nadalini <alessandro.nadalini3@unibo.it>
*/

#include "softex.hpp"
#include <cstring>
#include <cmath>

Softex::Softex(vp::ComponentConf &config)
    : vp::Component(config),
      reg_fsm_state(*this, "fsm_state", 32, true, SOFTEX_FSM_IDLE),
      reg_busy(*this, "busy", 1, true, 0)
{
    this->traces.new_trace("trace", &this->trace);

    this->new_master_port("out", &this->out);
    this->new_master_port("irq", &this->irq);

    this->in.set_req_meth(&Softex::cfg_slave);
    this->new_slave_port("input", &this->in);

    this->n_state_slots  = this->get_js_config()->get_child_int("n_state_slots");
    this->cache_slot_size= this->get_js_config()->get_child_int("cache_slot_size");
    if (this->n_state_slots <= 0)
    {
        this->n_state_slots = 2; // softex_pkg::N_CTRL_STATE_SLOTS default
    }
    if (this->cache_slot_size <= 0)
    {
        this->cache_slot_size = 32;
    }
    this->local_slots.resize(this->n_state_slots);

    this->queue_depth = this->get_js_config()->get_child_int("queue_depth");
    if ((int32_t)this->queue_depth <= 0)
    {
        this->queue_depth = 8;
    }

    this->fsm_event = this->event_new(&Softex::fsm_handler);
    this->mem_req = this->out.req_new(0, NULL, 0, false);

    // async slot pool: queue_depth+1 entries
    this->request_denied = false;
    this->out.set_resp_meth(&Softex::mem_response);
    this->out.set_grant_meth(&Softex::mem_grant);

    int pool_size = (int)this->queue_depth + 1;
    this->req_slots.resize(pool_size);
    for (int i = 0; i < pool_size; i++)
    {
        ReqSlot &slot = this->req_slots[i];
        slot.req = this->out.req_new(0, NULL, 0, false);
        slot.phase = 0;
        slot.beat_idx = 0;
        slot.valid_bytes = 0;
        slot.available_at = 0;
        memset(slot.buf, 0, sizeof(slot.buf));
        // arg(0) = slot id, arg(1) = back-pointer, used by mem_response/mem_grant
        slot.req->arg_alloc(2);
        *((int *)slot.req->arg_get(0)) = i;
        *((void **)slot.req->arg_get(1)) = (void *)this;
    }

    this->reg_fsm_state.set(SOFTEX_FSM_IDLE);
    this->reg_busy.set(0);

    this->job_id_counter = 0;
    this->job_state = 0;
    this->job_pending = 0;
    this->cxt_cfg_ptr = 0;
    this->cxt_use_ptr = 0;
    this->cxt_job_id[0] = -1;
    this->cxt_job_id[1] = -1;
    this->running_job_id = -1;

    ff_init_double(&this->running_max, -INFINITY, SoftexFormat::in());
    ff_init_double(&this->running_sum, 0.0, SoftexFormat::acc());
    ff_init_double(&this->reciprocal, 0.0, SoftexFormat::acc());

    this->trace.msg(vp::Trace::LEVEL_INFO, "Softex model built (n_state_slots=%d, queue_depth=%d)\n",
        this->n_state_slots, this->queue_depth);
}

void Softex::reset(bool active)
{
    if (active)
    {
        this->reg_fsm_state.set(SOFTEX_FSM_IDLE);
        this->reg_busy.set(0);

        for (int i = 0; i < SOFTEX_N_CONTEXT; i++)
        {
            this->contexts[i] = SoftexJob();
            this->cxt_job_id[i] = -1;
        }
        this->job_id_counter = 0;
        this->job_state = 0;
        this->job_pending = 0;
        this->cxt_cfg_ptr = 0;
        this->cxt_use_ptr = 0;
        this->running_job_id = -1;

        this->slot_cache_base_addr = 0;
        for (auto &s : this->local_slots)
        {
            s = SoftexSlot();
        }

        ff_init_double(&this->running_max, -INFINITY, SoftexFormat::in());
        ff_init_double(&this->running_sum, 0.0, SoftexFormat::acc());
        ff_init_double(&this->reciprocal, 0.0, SoftexFormat::acc());

        int64_t now = this->clock.get_cycles();
        for (auto &slot : this->req_slots)
        {
            slot.available_at = now;
        }
        this->request_denied = false;

        this->irq_pending = false;
    }
}

/**************************************************************************
* REGISTER ACCESS
**************************************************************************/

vp::IoReqStatus Softex::cfg_slave(vp::Block *__this, vp::IoReq *req)
{
    Softex *_this = (Softex *)__this;
    uint32_t address = req->get_addr();
    uint32_t size = req->get_size();

    if (size != 4)
    {
        _this->trace.fatal("Softex only supports 32-bit register accesses (got size=%d)\n", size);
        return vp::IO_REQ_INVALID;
    }

    if (req->get_is_write())
    {
        uint32_t data = *((uint32_t *)req->get_data());
        _this->handle_cfg_write(address, data);
    }
    else
    {
        uint32_t value = 0;
        _this->handle_cfg_read(address, &value);
        memcpy(req->get_data(), &value, 4);
    }

    return vp::IO_REQ_OK;
}

void Softex::handle_cfg_read(uint32_t offset, uint32_t *value)
{
    *value = 0;

    switch (offset)
    {
    case SOFTEX_ACQUIRE:
    {
        int32_t id = this->acquire();
        *value = (uint32_t)id; // negative -> 0xffffffff.., matches sw's `< 0` check
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "ACQUIRE -> %d\n", id);
        break;
    }

    case SOFTEX_FINISHED:
        // real softex_ctrl.sv doesn't expose a separate "finished" flag beyond
        // busy_o; approximate it as "nothing running and nothing queued"
        *value = (this->reg_fsm_state.get() == SOFTEX_FSM_IDLE && this->job_pending == 0) ? 1 : 0;
        break;

    case SOFTEX_STATUS:
        // softex_ctrl.sv's busy_o: 1 whenever the control FSM isn't IDLE
        *value = this->reg_busy.get();
        break;

    case SOFTEX_RUNNING_JOB:
        *value = (uint32_t)this->running_job_id; // -1 -> 0xffffffff if none
        break;

    default:
        if (offset >= SOFTEX_REG_OFFS && offset < SOFTEX_REG_OFFS + SOFTEX_N_JOB_REGS * 4)
        {
            int reg = (offset - SOFTEX_REG_OFFS) / 4;
            SoftexJob &j = this->contexts[this->cxt_cfg_ptr];
            switch (reg)
            {
            case 0: *value = j.in_addr; break;
            case 1: *value = j.out_addr; break;
            case 2: *value = j.tot_len; break;
            case 3: *value = j.commands; break;
            case 4: *value = j.cache_base_addr; break;
            case 5: *value = j.cast_ctrl; break;
            }
        }
        else
        {
            this->trace.msg(vp::Trace::LEVEL_WARNING, "Read from unsupported offset %x\n", offset);
        }
    }
}

void Softex::handle_cfg_write(uint32_t offset, uint32_t data)
{
    if (offset >= SOFTEX_REG_OFFS && offset < SOFTEX_REG_OFFS + SOFTEX_N_JOB_REGS * 4)
    {
        // job registers always target the context currently open for
        // writing (cxt_cfg_ptr); real HW requires software to have gone
        // through ACQUIRE first, but we don't fault a stray write here.
        int reg = (offset - SOFTEX_REG_OFFS) / 4;
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Writing job reg #%d (ctx %d) = %x\n", reg, this->cxt_cfg_ptr, data);

        SoftexJob &j = this->contexts[this->cxt_cfg_ptr];
        switch (reg)
        {
        case 0: j.in_addr = data; break;
        case 1: j.out_addr = data; break;
        case 2: j.tot_len = data; break;
        case 3: j.commands = data; break;
        case 4: j.cache_base_addr = data; break;
        case 5: j.cast_ctrl = data; break;
        }
        return;
    }

    switch (offset)
    {
    case SOFTEX_TRIGGER:
        // softex's TRIGGER is a plain write (no commit/trigger-split bits):
        // always commit+start.
        this->trace.msg(vp::Trace::LEVEL_INFO, "TRIGGER (cfg ctx %d)\n", this->cxt_cfg_ptr);
        this->commit(true);
        break;

    case SOFTEX_ACQUIRE:
    case SOFTEX_FINISHED:
    case SOFTEX_STATUS:
    case SOFTEX_RUNNING_JOB:
        // read-only
        break;

    case SOFTEX_SOFT_CLEAR:
        this->trace.msg(vp::Trace::LEVEL_INFO, "SOFT_CLEAR\n");
        this->soft_clear();
        break;

    default:
        this->trace.msg(vp::Trace::LEVEL_WARNING, "Write to unsupported offset %x (%x)\n", offset, data);
    }
}

// ACQUIRE (read): assign a new job id and lock the write-side context.
int32_t Softex::acquire()
{
    if (this->job_state != 0)
    {
        // Already acquired, not yet committed: re-return the pending id.
        // (checked first to match hwpe_ctrl_target.sv's ACQUIRE-state
        // priority over queue-full)
        return this->cxt_job_id[this->cxt_cfg_ptr];
    }
    else if (this->job_pending == SOFTEX_N_CONTEXT)
    {
        // Queue full
        return -1;
    }
    else
    {
        // Free to acquire and room in the queue: hand out a new id
        int32_t id = (int32_t)this->job_id_counter++;
        this->cxt_job_id[this->cxt_cfg_ptr] = id;
        this->job_state = -2;
        return id;
    }
}

// TRIGGER write: commit the context currently being configured, and if the
// FSM is idle, start it right away.
void Softex::commit(bool start)
{
    if (this->job_state == 0)
    {
        // Software wrote TRIGGER without ever reading ACQUIRE: real HW
        // still queues the job but doesn't advance the job id counter
        // (only the retired-job counter does), mirrored here.
        if (this->job_pending < SOFTEX_N_CONTEXT)
        {
            this->cxt_job_id[this->cxt_cfg_ptr] = (int32_t)this->job_id_counter;
            this->job_state = -2;
        }
    }
    if (this->job_state != -2)
    {
        this->trace.msg(vp::Trace::LEVEL_WARNING,
            "TRIGGER dropped: job queue full (job_pending=%d)\n", this->job_pending);
        return;
    }

    this->job_pending++;
    this->job_state = 0;
    this->cxt_cfg_ptr = 1 - this->cxt_cfg_ptr;

    if (start && this->reg_fsm_state.get() == SOFTEX_FSM_IDLE)
    {
        this->start_next_job();
    }
}

// SOFT_CLEAR write: full reset of the job queue and control FSM. Real
// softex's SOFT_CLEAR doesn't define partial-clear modes, so this always clears
// everything
void Softex::soft_clear()
{
    this->job_state = 0;
    this->job_pending = 0;
    this->cxt_job_id[0] = -1;
    this->cxt_job_id[1] = -1;
    this->running_job_id = -1;
    this->cxt_cfg_ptr = 0;
    this->cxt_use_ptr = 0;
    this->job_id_counter = 0;
    for (int i = 0; i < SOFTEX_N_CONTEXT; i++)
    {
        this->contexts[i] = SoftexJob();
    }

    this->reg_fsm_state.set(SOFTEX_FSM_IDLE);
    this->reg_busy.set(0);
    if (this->irq.is_bound())
    {
        this->irq.sync(false);
    }
    this->irq_pending = false;

    if (this->fsm_event->is_enqueued())
    {
        this->event_cancel(this->fsm_event);
    }

    int64_t now = this->clock.get_cycles();
    for (auto &slot : this->req_slots)
    {
        slot.available_at = now;
    }
    this->request_denied = false;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Softex(config);
}
