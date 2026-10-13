// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include "cpu/iss_v2/include/iss.hpp"

// Cycles from the read data coming back to it being usable as an FPU operand
// (registered data FIFO)
#define SNITCH_SSR_READ_LATENCY 1
// Start-up cycle of a stream (the data movers are built with indirection
// support, which adds a stage in front of the address generator)
#define SNITCH_SSR_START_LATENCY 1

SnitchSsrStreamer::SnitchSsrStreamer(Iss &iss, SnitchSsr &top, int id)
: iss(iss), top(top), id(id),
  addr_signal(iss, "ssr" + std::to_string(id) + "/addr", 32, vp::SignalCommon::ResetKind::HighZ)
{
    iss.traces.new_trace("ssr" + std::to_string(id), &this->trace, vp::DEBUG);
    iss.new_master_port("ssr_" + std::to_string(id), &this->itf, (vp::Block *)this);
    this->fifo.resize(CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH);
    this->jobs.reserve(2);
}

void SnitchSsrStreamer::reset(bool active)
{
    if (active)
    {
        this->repeat = 0;
        for (int i = 0; i < 4; i++)
        {
            this->bounds[i] = 0;
            this->strides[i] = 0;
        }
        for (int i = 0; i < 8; i++)
        {
            this->ptrs[i] = 0;
        }
        this->jobs.clear();
        this->fifo_head = 0;
        this->fifo_tail = 0;
        this->fifo_count = 0;
        this->repeat_count = 0;
        this->last_pop_id = -1;
        this->last_pop_data = 0;
        this->free_reqs = NULL;
        for (int i = 0; i < CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH; i++)
        {
            SnitchSsrReq *req = &this->reqs[i];
            req->req.set_data((uint8_t *)&req->data);
            req->next = this->free_reqs;
            this->free_reqs = req;
        }
        this->nb_pending_reqs = 0;
        this->denied_req = NULL;
        this->sync_reqs.clear();
    }
}

bool SnitchSsrStreamer::cfg_write(int reg, iss_reg_t value)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Configuration write (reg: %d, value: 0x%lx)\n",
        reg, value);

    if (reg == SNITCH_SSR_REG_REPEAT)
    {
        this->repeat = value;
    }
    else if (reg >= SNITCH_SSR_REG_BOUNDS && reg < SNITCH_SSR_REG_BOUNDS + 4)
    {
        this->bounds[reg - SNITCH_SSR_REG_BOUNDS] = value;
    }
    else if (reg >= SNITCH_SSR_REG_STRIDES && reg < SNITCH_SSR_REG_STRIDES + 4)
    {
        this->strides[reg - SNITCH_SSR_REG_STRIDES] = value;
    }
    else if (reg >= SNITCH_SSR_REG_RPTR && reg < SNITCH_SSR_REG_WPTR + 4)
    {
        // A stream is already waiting behind the current one, retry later
        if (this->jobs.size() >= 2) return false;

        bool is_write = reg >= SNITCH_SSR_REG_WPTR;
        int dims = reg - (is_write ? SNITCH_SSR_REG_WPTR : SNITCH_SSR_REG_RPTR) + 1;

        this->ptrs[reg - SNITCH_SSR_REG_RPTR] = value;

        SnitchSsrJob job;
        job.is_write = is_write;
        job.dims = dims;
        job.repeat = this->repeat;
        job.ptr = value;
        job.addr_done = false;
        job.start_cycle = -1;
        int64_t nb_elems = 1;
        for (int i = 0; i < 4; i++)
        {
            job.bounds[i] = this->bounds[i];
            job.strides[i] = this->strides[i];
            job.cnt[i] = 0;
            if (i < dims) nb_elems *= (int64_t)this->bounds[i] + 1;
        }
        job.fp_remaining = is_write ? nb_elems : nb_elems * ((int64_t)this->repeat + 1);

        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Launching stream (is_write: %d, dims: %d, "
            "ptr: 0x%lx, nb_elems: %ld, repeat: %ld)\n", is_write, dims, value, nb_elems,
            this->repeat);

        this->jobs.push_back(job);
        this->top.activate();
    }

    return true;
}

iss_reg_t SnitchSsrStreamer::cfg_read(int reg)
{
    if (reg == SNITCH_SSR_REG_STATUS)
    {
        // Bit 31: done
        return (iss_reg_t)this->jobs.empty() << 31;
    }
    else if (reg == SNITCH_SSR_REG_REPEAT)
    {
        return this->repeat;
    }
    else if (reg >= SNITCH_SSR_REG_BOUNDS && reg < SNITCH_SSR_REG_BOUNDS + 4)
    {
        return this->bounds[reg - SNITCH_SSR_REG_BOUNDS];
    }
    else if (reg >= SNITCH_SSR_REG_STRIDES && reg < SNITCH_SSR_REG_STRIDES + 4)
    {
        return this->strides[reg - SNITCH_SSR_REG_STRIDES];
    }
    else if (reg >= SNITCH_SSR_REG_RPTR && reg < SNITCH_SSR_REG_WPTR + 4)
    {
        return this->ptrs[reg - SNITCH_SSR_REG_RPTR];
    }
    return 0;
}

uint64_t SnitchSsrStreamer::pop(int64_t insn_id)
{
    // Several operands of the same instruction on the same SSR read the same
    // element
    if (insn_id == this->last_pop_id)
    {
        return this->last_pop_data;
    }

    SnitchSsrJob *job = this->fp_job();
    if (job == NULL || job->is_write || this->fifo_count == 0 || !this->fifo[this->fifo_head].valid)
    {
        // The FP subsystem checks the data is there before issuing, this is
        // a read it did not see (e.g. an implicit operand)
        this->trace.force_warning("SSR read without data\n");
        return 0;
    }

    SnitchSsrSlot &slot = this->fifo[this->fifo_head];
    uint64_t data = slot.data;

    this->last_pop_id = insn_id;
    this->last_pop_data = data;

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Read data (value: 0x%lx, repeat: %ld)\n",
        data, this->repeat_count);

    job->fp_remaining--;
    this->repeat_count++;
    if (this->repeat_count > job->repeat)
    {
        // Last repetition, free the FIFO credit
        this->repeat_count = 0;
        this->fifo_head = (this->fifo_head + 1) % CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH;
        this->fifo_count--;
    }

    this->job_check_end();
    this->top.activate();

    return data;
}

void SnitchSsrStreamer::push(uint64_t data, int64_t ready_cycle)
{
    SnitchSsrJob *job = this->fp_job();
    if (job == NULL || !job->is_write ||
        this->fifo_count == CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH)
    {
        this->trace.force_warning("SSR write without room\n");
        return;
    }

    SnitchSsrSlot &slot = this->fifo[this->fifo_tail];

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Write data (value: 0x%lx, ready: %ld)\n",
        data, ready_cycle);

    slot.data = data;
    slot.valid = true;
    slot.ready_cycle = ready_cycle;
    this->fifo_tail = (this->fifo_tail + 1) % CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH;
    this->fifo_count++;

    job->fp_remaining--;

    this->job_check_end();
    this->top.activate();
}

void SnitchSsrStreamer::job_check_end()
{
    while (!this->jobs.empty() && this->jobs[0].fp_remaining == 0 && this->jobs[0].addr_done)
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Stream done\n");
        this->jobs.erase(this->jobs.begin());
    }
}

bool SnitchSsrStreamer::addr_next(SnitchSsrJob &job)
{
    // Affine loop nest: the innermost dimension which has not reached its
    // bound increments, the ones below wrap. The pointer moves by the stride
    // of the dimension which increments (the strides are relative to the
    // wrapped position, see snrt_ssr_loop_*).
    for (int i = 0; i < job.dims; i++)
    {
        if (job.cnt[i] < job.bounds[i])
        {
            job.cnt[i]++;
            job.ptr += job.strides[i];
            return true;
        }
        job.cnt[i] = 0;
    }
    return false;
}

bool SnitchSsrStreamer::is_busy()
{
    return !this->jobs.empty() || this->nb_pending_reqs > 0;
}

bool SnitchSsrStreamer::handle(int64_t cycle)
{
    for (size_t i = 0; i < this->sync_reqs.size();)
    {
        SnitchSsrReq *req = this->sync_reqs[i];
        if (req->timestamp <= cycle)
        {
            this->sync_reqs[i] = this->sync_reqs.back();
            this->sync_reqs.pop_back();
            this->req_end(req, cycle);
        }
        else
        {
            i++;
        }
    }

    // Waiting for the retry of a denied request
    if (this->denied_req) return true;

    SnitchSsrJob *job = this->addr_job();
    if (job == NULL) return this->is_busy();

    // A stream waiting behind another one can start its accesses only if it
    // goes in the same direction, the data FIFO is shared
    if (job != &this->jobs[0] && job->is_write != this->jobs[0].is_write) return true;

    if (job->start_cycle == -1)
    {
        job->start_cycle = cycle + SNITCH_SSR_START_LATENCY;
    }
    if (cycle < job->start_cycle) return true;

    if (this->free_reqs == NULL) return true;

    SnitchSsrReq *req;
    iss_reg_t addr = job->ptr;
    // The data movers access 64-bit words. A 32-bit aligned element is read
    // from its word and shifted, written with only its bytes.
    int offset = addr & 7;

    if (job->is_write)
    {
        if (this->fifo_count == 0) return true;
        SnitchSsrSlot &slot = this->fifo[this->fifo_head];
        if (slot.ready_cycle > cycle) return true;

        req = this->free_reqs;
        this->free_reqs = req->next;
        req->data = slot.data;
        req->slot = -1;
        this->fifo_head = (this->fifo_head + 1) % CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH;
        this->fifo_count--;

        req->req.prepare();
        req->req.set_addr(addr);
        req->req.set_size(8 - offset);
        req->req.set_opcode(vp::IoReqOpcode::WRITE);
    }
    else
    {
        if (this->fifo_count == CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH) return true;

        req = this->free_reqs;
        this->free_reqs = req->next;
        req->slot = this->fifo_tail;
        req->data = 0;
        this->fifo[this->fifo_tail].valid = false;
        this->fifo_tail = (this->fifo_tail + 1) % CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH;
        this->fifo_count++;

        req->req.prepare();
        req->req.set_addr(addr - offset);
        req->req.set_size(8);
        req->req.set_opcode(vp::IoReqOpcode::READ);
        req->offset = offset;
    }
    req->req.set_data((uint8_t *)&req->data);
    req->req.set_resp_status(vp::IO_RESP_OK);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Memory access (addr: 0x%lx, is_write: %d)\n",
        addr, job->is_write);
    this->addr_signal.set_and_release(addr);

    if (!this->addr_next(*job))
    {
        job->addr_done = true;
        this->job_check_end();
    }

    this->nb_pending_reqs++;
    this->send(req);

    return true;
}

void SnitchSsrStreamer::send(SnitchSsrReq *req)
{
    vp::IoReqStatus status = this->itf.req(&req->req);

    if (status == vp::IO_REQ_DONE)
    {
        req->timestamp = this->iss.clock.get_cycles() + req->req.get_latency();
        this->sync_reqs.push_back(req);
        this->top.activate();
    }
    else if (status == vp::IO_REQ_DENIED)
    {
        this->denied_req = req;
    }
}

void SnitchSsrStreamer::req_end(SnitchSsrReq *req, int64_t cycle)
{
    if (req->req.get_resp_status() == vp::IO_RESP_INVALID)
    {
        this->trace.force_warning("Invalid SSR access (addr: 0x%lx)\n", req->req.get_addr());
    }

    if (req->slot != -1)
    {
        SnitchSsrSlot &slot = this->fifo[req->slot];
        slot.data = req->data >> (req->offset * 8);
        slot.valid = true;
        slot.ready_cycle = cycle + SNITCH_SSR_READ_LATENCY;
    }

    this->nb_pending_reqs--;
    req->next = this->free_reqs;
    this->free_reqs = req;
}

void SnitchSsrStreamer::retry(vp::Block *__this, vp::IoRetryChannel)
{
    SnitchSsrStreamer *_this = (SnitchSsrStreamer *)__this;
    SnitchSsrReq *req = _this->denied_req;
    if (req == NULL) return;

    // Re-issued synchronously, the accept window of the arbiters is only
    // open during this call
    _this->denied_req = NULL;
    _this->send(req);
    _this->top.activate();
}

vp::IoRespAck SnitchSsrStreamer::response(vp::Block *__this, vp::IoReq *req)
{
    SnitchSsrStreamer *_this = (SnitchSsrStreamer *)__this;
    _this->req_end((SnitchSsrReq *)req, _this->iss.clock.get_cycles());
    _this->top.activate();
    return vp::IO_RESP_ACCEPTED;
}


SnitchSsr::SnitchSsr(Iss &iss)
: iss(iss), fsm_event(&iss, &SnitchSsr::fsm_handler)
{
    iss.traces.new_trace("ssr", &this->trace, vp::DEBUG);

    for (int i = 0; i < CONFIG_GVSOC_ISS_SNITCH_NB_SSRS; i++)
    {
        this->streamers.push_back(new SnitchSsrStreamer(iss, *this, i));
    }

    iss.csr.declare_csr(&this->csr, "ssr", 0x7C0);
    this->csr.register_callback(std::bind(&SnitchSsr::csr_update, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3));
}

void SnitchSsr::reset(bool active)
{
    for (SnitchSsrStreamer *streamer: this->streamers)
    {
        streamer->reset(active);
    }
    if (active)
    {
        this->enabled = false;
        this->fsm_event.disable();
    }
}

bool SnitchSsr::csr_update(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->enabled = value & 1;
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "SSR %s\n", this->enabled ? "enabled" : "disabled");
    }
    else
    {
        value = this->enabled;
    }
    return false;
}

bool SnitchSsr::cfg_write(int reg, int ssr, iss_reg_t value)
{
    // SSR 31 writes all of them
    if (ssr == 31)
    {
        // All of them or none, a pointer write is retried until they all
        // accept it
        for (SnitchSsrStreamer *streamer: this->streamers)
        {
            if (reg >= SNITCH_SSR_REG_RPTR && !streamer->can_launch()) return false;
        }
        for (SnitchSsrStreamer *streamer: this->streamers)
        {
            streamer->cfg_write(reg, value);
        }
        return true;
    }
    else if (ssr < CONFIG_GVSOC_ISS_SNITCH_NB_SSRS)
    {
        return this->streamers[ssr]->cfg_write(reg, value);
    }
    return true;
}

iss_reg_t SnitchSsr::cfg_read(int reg, int ssr)
{
    if (ssr < CONFIG_GVSOC_ISS_SNITCH_NB_SSRS)
    {
        return this->streamers[ssr]->cfg_read(reg);
    }
    return 0;
}

bool SnitchSsr::is_busy()
{
    for (SnitchSsrStreamer *streamer: this->streamers)
    {
        if (streamer->is_busy()) return true;
    }
    return false;
}

void SnitchSsr::activate()
{
    this->fsm_event.enable();
}

void SnitchSsr::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Iss *iss = (Iss *)__this;
    SnitchSsr *_this = &iss->arch.ssr;
    int64_t cycle = iss->clock.get_cycles();
    bool busy = false;

    for (SnitchSsrStreamer *streamer: _this->streamers)
    {
        busy |= streamer->handle(cycle);
    }

    if (!busy)
    {
        _this->fsm_event.disable();
    }
}
