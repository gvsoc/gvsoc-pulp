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
#include <cmath>
#include <cstring>
#include <algorithm>

void Softex::enqueue_fsm(int64_t cycles)
{
    if (cycles < 1)
    {
        cycles = 1;
    }
    this->event_enqueue(this->fsm_event, cycles);
}

// Load the queued job in cxt_use_ptr and kick off the FSM (IDLE -> ...).
void Softex::start_next_job()
{
    this->running_job_id = this->cxt_job_id[this->cxt_use_ptr];
    this->job = this->contexts[this->cxt_use_ptr];
    SoftexJob &j = this->job;

    this->trace.msg(vp::Trace::LEVEL_INFO, "Starting job %d (ctx %d, cmd=%x)\n",
        this->running_job_id, this->cxt_use_ptr, j.commands);

    if (j.set_cache_addr())
    {
        this->slot_cache_base_addr = j.cache_base_addr;
    }

    if (j.no_op())
    {
        this->reg_fsm_state.set(SOFTEX_FSM_FINISHED);
        this->reg_busy.set(1);
        this->enqueue_fsm(SoftexLatency::CTRL_LAT);
        return;
    }

    bool need_slot = j.acc_only() || j.div_only();

    if (need_slot)
    {
        if (j.acquire_slot())
        {
            // Fresh partial op: reserve a slot, no data to fetch.
            this->cur_slot = SoftexSlot();
            this->cur_slot.valid = true;
            this->cur_slot.id = j.slot_id();
            this->cur_slot.maximum = -INFINITY;
            this->cur_slot.denominator = 0.0;
        }
        else
        {
            // Resuming: slot must already exist (locally or in the
            // memory-backed cache). Cost is paid via WAIT_SLOT_VALID.
            if (!this->slot_lookup(j.slot_id(), this->cur_slot))
            {
                this->reg_fsm_state.set(SOFTEX_FSM_WAIT_SLOT_VALID);
                this->reg_busy.set(1);
                this->slot_fill_from_mem(j.slot_id(), this->slot_cache_base_addr, this->cur_slot);
                this->enqueue_fsm(SoftexLatency::CTRL_LAT + 4); // approx round-trip to fetch 6B from mem
                return;
            }
        }
    }
    else
    {
        this->cur_slot = SoftexSlot();
    }

    ff_init_double(&this->running_max, this->cur_slot.maximum, SoftexFormat::in());
    ff_init_double(&this->running_sum, this->cur_slot.denominator, SoftexFormat::acc());

    if (j.div_only())
    {
        // reciprocal was already computed and stashed in cur_slot.denominator
        // by a previous ACC_ONLY(+LAST)/full run (see FINISHED below)
        ff_init_double(&this->reciprocal, this->cur_slot.denominator, SoftexFormat::acc());
        this->reg_fsm_state.set(SOFTEX_FSM_DIVIDING);
    }
    else
    {
        this->reg_fsm_state.set(SOFTEX_FSM_ACCUMULATION);
    }
    this->reg_busy.set(1);
    this->begin_datapath_phase();
}

// Beat/element bookkeeping + kick off the async issue engine for whichever
// phase reg_fsm_state currently holds (ACCUMULATION or DIVIDING). Called
// from start_next_job() (fast path), from the WAIT_SLOT_VALID continuation,
// and from WAIT_INVERSION when a full (non-acc_only/div_only) job moves on
// to normalize its input a second time.
void Softex::begin_datapath_phase()
{
    SoftexJob &j = this->job;

    uint32_t elem_size_in = j.cast_input() ? 1 : 2;
    this->num_elements = j.tot_len / (elem_size_in ? elem_size_in : 1);
    this->n_beats = (this->num_elements + SOFTEX_N_ROWS - 1) / SOFTEX_N_ROWS;
    this->beat_size_in = elem_size_in * SOFTEX_N_ROWS;
    this->beat_size_out = (j.cast_output() ? 1 : 2) * SOFTEX_N_ROWS;

    this->beats_issued = 0;
    this->beats_completed = 0;

    if (this->n_beats == 0)
    {
        // degenerate empty job: nothing to stream, finish immediately
        if (this->reg_fsm_state.get() == SOFTEX_FSM_ACCUMULATION)
        {
            this->reg_fsm_state.set(SOFTEX_FSM_WAIT_DATAPATH_EMPTY);
        }
        else
        {
            this->reg_fsm_state.set(SOFTEX_FSM_FINISHED);
        }
        this->enqueue_fsm(SoftexLatency::CTRL_LAT);
        return;
    }

    this->pump_issue();
}

/**************************************************************************
* Control-state FSM tick
* Only fires for the "control" states -- the data
* movement states (ACCUMULATION/DIVIDING) never enqueue fsm_event
* themselves; they run entirely off pump_issue()/handle_mem_response()
* (see softex_stream.cpp) and jump straight to the next control state
* once the last beat's response lands.
**************************************************************************/
void Softex::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Softex *_this = (Softex *)__this;
    _this->fsm_step();
}

void Softex::fsm_step()
{
    switch (this->reg_fsm_state.get())
    {
    case SOFTEX_FSM_IDLE:
        // Deassert irq, then auto-continue the next queued job
        if (this->irq.is_bound())
        {
            this->irq.sync(false);
        }
        this->irq_pending = false;
        if (this->job_pending > 0)
        {
            this->start_next_job();
        }
        return;

    case SOFTEX_FSM_WAIT_SLOT_VALID:
    {
        this->cur_slot.valid = true;
        ff_init_double(&this->running_max, this->cur_slot.maximum, SoftexFormat::in());
        ff_init_double(&this->running_sum, this->cur_slot.denominator, SoftexFormat::acc());

        SoftexJob &j = this->job;
        if (j.div_only())
        {
            ff_init_double(&this->reciprocal, this->cur_slot.denominator, SoftexFormat::acc());
            this->reg_fsm_state.set(SOFTEX_FSM_DIVIDING);
        }
        else
        {
            this->reg_fsm_state.set(SOFTEX_FSM_ACCUMULATION);
        }
        this->begin_datapath_phase();
        return;
    }

    case SOFTEX_FSM_WAIT_DATAPATH_EMPTY:
        // waiting for the last accumulator FMA / reduction result to drain
        this->reg_fsm_state.set(SOFTEX_FSM_WAIT_ACCUMULATION);
        this->enqueue_fsm(SoftexLatency::FMA_REGS_ACC);
        return;

    case SOFTEX_FSM_WAIT_ACCUMULATION:
    {
        SoftexJob &j = this->job;
        if (j.acc_only() && !j.last())
        {
            // stash the partial max/denominator (not yet inverted) and stop
            this->reg_fsm_state.set(SOFTEX_FSM_FINISHED);
            this->enqueue_fsm(SoftexLatency::CTRL_LAT);
        }
        else
        {
            this->reg_fsm_state.set(SOFTEX_FSM_WAIT_INVERSION);
            this->enqueue_fsm(SoftexLatency::INV_LATENCY);
        }
        return;
    }

    case SOFTEX_FSM_WAIT_INVERSION:
    {
        // Newton-Raphson reciprocal of the accumulated denominator
        // (softex_pkg::N_NEWTON_ITERS iterations, correctly rounded FP32
        // arithmetic at each step via flexfloat -- see newton_reciprocal()).
        this->reciprocal = this->newton_reciprocal(this->running_sum);

        SoftexJob &j = this->job;
        if (j.acc_only())
        {
            this->reg_fsm_state.set(SOFTEX_FSM_FINISHED);
            this->enqueue_fsm(SoftexLatency::CTRL_LAT);
        }
        else
        {
            this->reg_fsm_state.set(SOFTEX_FSM_DIVIDING);
            this->begin_datapath_phase();
        }
        return;
    }

    case SOFTEX_FSM_FINISHED:
    {
        SoftexJob &j = this->job;

        // mirrors softex_ctrl.sv's state_slot_en / update_op logic
        if (j.acc_only() || (j.div_only() && j.last()))
        {
            bool free_it = j.div_only() && j.last();
            flexfloat_t stored = (j.acc_only() && !j.last()) ? this->running_sum : this->reciprocal;
            this->slot_update(j.slot_id(), ff_get_double(&this->running_max), ff_get_double(&stored), free_it);
        }

        this->trace.msg(vp::Trace::LEVEL_INFO, "Job %d finished\n", this->running_job_id);

        // Retire the job that just finished
        this->cxt_job_id[this->cxt_use_ptr] = -1;
        this->cxt_use_ptr = 1 - this->cxt_use_ptr;
        if (this->job_pending > 0)
        {
            this->job_pending--;
        }

        this->irq_pending = true;
        if (this->irq.is_bound())
        {
            this->irq.sync(true);
        }

        this->reg_fsm_state.set(SOFTEX_FSM_IDLE);
        this->reg_busy.set(0);
        this->enqueue_fsm(1);
        return;
    }
    }
}
