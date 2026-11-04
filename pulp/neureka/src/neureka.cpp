/*
 * Copyright (C) 2020-2022  GreenWaves Technologies, ETH Zurich, University of Bologna
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
 * Authors: Francesco Conti, University of Bologna & GreenWaves Technologies (f.conti@unibo.it)
 *          Germain Haugou, GreenWaves Technologies (germain.haugou@greenwaves-technologies.com)
 *          Arpan Suravi Prasad, ETH Zurich (prasadar@iis.ee.ethz.ch)
 */

#include <neureka.hpp>

using namespace std::placeholders;

Neureka::Neureka(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->TP_IN           = this->get_js_config()->get_child_int("tp_in");
    this->TP_IN_S         = this->get_js_config()->get_child_int("tp_in_s");
    this->TP_OUT          = this->get_js_config()->get_child_int("tp_out");
    this->QA_IN           = this->get_js_config()->get_child_int("qa_in");
    this->QA_OUT          = this->get_js_config()->get_child_int("qa_out");
    this->H_SIZE          = this->get_js_config()->get_child_int("h_size");
    this->W_SIZE          = this->get_js_config()->get_child_int("w_size");
    this->NR_COLUMN       = this->H_SIZE*this->W_SIZE;
    this->COLUMN_SIZE     = this->get_js_config()->get_child_int("column_size");
    this->BLOCK_SIZE      = this->get_js_config()->get_child_int("block_size");
    this->F_BUFFER_SIZE   = this->get_js_config()->get_child_int("f_buffer_size");
    this->FILTER_SIZE     = this->get_js_config()->get_child_int("filter_size");
    this->SHIFT_CYCLES    = this->get_js_config()->get_child_int("shift_cycles");
    this->OVERHEAD_LD_1X1 = this->get_js_config()->get_child_int("overhead_ld_1x1");
    this->OVERHEAD_LD_3X3 = this->get_js_config()->get_child_int("overhead_ld_3x3");
    this->OVERHEAD_MV     = this->get_js_config()->get_child_int("overhead_mv");
    this->QUANT_PER_CYCLE = this->get_js_config()->get_child_int("quant_per_cycle");

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_reg("fsm_state", &this->state, 32);//public in hpp
    this->new_reg("neureka_busy", &this->activity, 8);//public in hpp
    this->activity.set(0);//public in hpp
    this->state.set(IDLE);//public in hpp
    this->new_master_port("out", &this->out);//public in hpp
    this->new_master_port("wmem_out", &this->wmem_out);//public in hpp
    this->new_master_port("irq", &this->irq);//private in hpp connected to the cluster event unit
    this->new_slave_port("input", &this->in); // how to change this name? //private in hpp linked to preipheral ico at the cluster
    this->fsm_start_event = this->event_new(&Neureka::fsm_start_handler);//private in hpp
    this->fsm_event = this->event_new(&Neureka::fsm_handler);//private in hpp
    this->fsm_end_event = this->event_new(&Neureka::fsm_end_handler);//private in hpp
    this->trace_level = NEUREKA_L0_JOB_START_END;//public in hpp
    this->trace_format = 0;//public in hpp

}

void Neureka::reset(bool active)
{
    // All registers should be initialized here, so that cluster reset is properly working
    // This will be called first with active=1 and then with active=0
    this->psum_block      = xt::zeros<int64_t>({this->NR_COLUMN, this->COLUMN_SIZE});
    this->psum_column     = xt::zeros<int64_t>({this->NR_COLUMN});
    this->accum           = xt::zeros<int64_t>({this->TP_OUT, this->NR_COLUMN});
    this->x_buffer        = xt::zeros<uint8_t>({this->F_BUFFER_SIZE, this->F_BUFFER_SIZE, this->TP_IN});
    this->x_array         = xt::zeros<uint8_t>({this->NR_COLUMN, this->COLUMN_SIZE, this->TP_IN}); 
    this->weight          = xt::zeros<uint8_t>({this->FILTER_SIZE*this->FILTER_SIZE, (this->TP_IN/8)});
    this->dw_weight_buffer= xt::zeros<uint8_t>({8, 32});//<8 bits of weight so 8 cycles, 32*8 is the bw offered>
    this->nqs             = xt::zeros<uint8_t>({this->TP_OUT});
    this->clear_jobs();
    this->job_running     = 0;
    this->start_cycles    = 0;
    this->end_cycles      = 0x7FFFFFFF;
    this->state_phase_track_state = IDLE;
    this->state_phase_track_cycles = 0;
    this->state_phase_track_valid = false;
    this->state_phase_track_start_cycle = 0;
    this->stream_pending = false;
    this->stream_post_latency = 0;
    this->in_fsm = false;
}

void Neureka::stream_issue(uint64_t addr, uint8_t *data, int size, bool is_write, bool wmem)
{
    assert(!this->stream_pending);
    this->stream_pending = true;
    this->stream_port = wmem ? &this->wmem_out : &this->out;

    this->stream_req.set_addr(addr);
    this->stream_req.set_data(data);
    this->stream_req.set_size(size);
    this->stream_req.set_is_write(is_write);
    this->stream_req.prepare();

    vp::IoReqStatus status = this->stream_port->req(&this->stream_req);
    if (status == vp::IO_REQ_DONE)
    {
        this->stream_done();
    }
    // GRANTED: wait for the response, DENIED: wait for the retry
}

void Neureka::stream_retry_port(vp::IoMaster *port)
{
    if (!this->stream_pending || this->stream_port != port) return;

    // Synchronous re-issue inside the retry callback, the HCI only forwards
    // it during its election
    vp::IoReqStatus status = port->req(&this->stream_req);
    if (status == vp::IO_REQ_DONE)
    {
        this->stream_done();
    }
}

void Neureka::stream_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
    Neureka *_this = (Neureka *)__this;
    _this->stream_retry_port(&_this->out);
}

void Neureka::wmem_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
    Neureka *_this = (Neureka *)__this;
    _this->stream_retry_port(&_this->wmem_out);
}

vp::IoRespAck Neureka::stream_resp(vp::Block *__this, vp::IoReq *req)
{
    Neureka *_this = (Neureka *)__this;
    _this->stream_done();
    return vp::IO_RESP_ACCEPTED;
}

vp::IoRespAck Neureka::wmem_resp(vp::Block *__this, vp::IoReq *req)
{
    Neureka *_this = (Neureka *)__this;
    _this->stream_done();
    return vp::IO_RESP_ACCEPTED;
}

// End of a streamer access. If it completed inline while the FSM step was
// sending it, the step goes on as with the io v1 model. Otherwise the FSM
// resumes after the latency of the step which sent it, at least one cycle
// later since the HCI port takes one access per cycle. The streamer is
// pipelined against the memory, whose response comes one cycle after the
// request: only the latency above that one slows it down.
void Neureka::stream_done()
{
    this->stream_pending = false;

    if (this->in_fsm) return;

    int64_t latency = this->stream_req.get_latency();
    int64_t delay = this->stream_post_latency + (latency > 1 ? latency - 1 : 0);
    if (delay < 1)
    {
        delay = 1;
    }
    this->stream_post_latency = 0;
    if (!this->fsm_event->is_enqueued())
    {
        this->event_enqueue(this->fsm_event, delay);
    }
}

// The `hwpe_slave` member function models an access to the NEUREKA SLAVE interface
vp::IoReqStatus Neureka::hwpe_slave(vp::Block *__this, vp::IoReq *req)
{
    Neureka *_this = (Neureka *)__this;

    if (trace_at_least(_this->trace_level, NEUREKA_L3_ALL)) {
      _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request (addr: 0x%x, size: 0x%x, is_write: %d, data: %p\n", req->get_addr(), req->get_size(), req->get_is_write(), req->get_data());
    }
    uint8_t *data = req->get_data(); // size depends on data get_size

    // Dispatch the register file access to the correct function
    if(req->get_is_write()) {
        if(((req->get_addr() & 0xfff) - 0x20) >> 2 == NEUREKA_SPECIAL_TRACE_REG) {
            if(*data == 0) {
                _this->trace_level = NEUREKA_L0_JOB_START_END;
                _this->trace.msg("Setting tracing level to NEUREKA_L0_JOB_START_END\n");
            }
            else if(*data == 1) {
                _this->trace_level = NEUREKA_L1_CONFIG;
                _this->trace.msg("Setting tracing level to NEUREKA_L1_CONFIG\n");
            }
            else if(*data == 2) {
                _this->trace_level = NEUREKA_L2_STREAM_INOUT;
                _this->trace.msg("Setting tracing level to NEUREKA_L2_STREAM_INOUT\n");
            }
            else if(*data == 3) {
                _this->trace_level = NEUREKA_L3_ALL;
                _this->trace.msg("Setting tracing level to NEUREKA_L3_ALL\n");
            }
            else {
                _this->trace_level = NEUREKA_L4_STATE_PHASE_SUMMARY;
                _this->trace.msg("Setting tracing level to NEUREKA_L4_STATE_PHASE_SUMMARY\n");
            }
            return vp::IO_REQ_DONE;
        }
        else if(((req->get_addr() & 0xfff) - 0x20) >> 2 == NEUREKA_SPECIAL_FORMAT_TRACE_REG) {
            _this->trace_format = *data;
            _this->trace.msg("Setting tracing format to %s\n", *data?"Hex":"Dec");
            return vp::IO_REQ_DONE;
        }
        else if((req->get_addr() & 0x17f) == 0x0) {
            _this->commit();
            if (!_this->job_running && !_this->fsm_start_event->is_enqueued() && *(uint32_t *) data == 0) {
                _this->event_enqueue(_this->fsm_start_event, 1);
            }
        }
        else if((req->get_addr() & 0x17f) < 0x20) {
            // Other control registers: only the soft clear has an effect
            if((req->get_addr() & 0x17f) == 0x14) {
                _this->clear_jobs();
            }
        }
        else {
            if (trace_at_least(_this->trace_level, NEUREKA_L3_ALL)) {
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "offset: %d data: %08x\n", ((req->get_addr() & 0x17f) - 0x20) >> 2, *(uint32_t *) data);
            }
            _this->regfile_wr(((req->get_addr() & 0x17f) - 0x20)>> 2, *(uint32_t *) data);
        }
    }
    else {
        if((req->get_addr() & 0x17f) == 0x4) {
            *(uint32_t *) data = _this->acquire();
            if (trace_at_least(_this->trace_level, NEUREKA_L3_ALL)) {
                _this->trace.msg("Returning %x\n", *(uint32_t *) data);
            }
        }
        else if((req->get_addr() & 0x17f) == 0xc) {
            *(uint32_t *) data = ((_this->cxt_job_id[0]>=0?0x1:0)|(_this->cxt_job_id[1]>=0?0x100:0));
            if (trace_at_least(_this->trace_level, NEUREKA_L3_ALL)) {
                _this->trace.msg("Returning %x\n", *(uint32_t *) data);
            }
        }
        else if((req->get_addr() & 0x17f) == 0x10) {
            // Returns the active running job or the last jobid that was run
            *(uint32_t *) data = _this->running_job_id;
            if (trace_at_least(_this->trace_level, NEUREKA_L3_ALL)) {
                _this->trace.msg("Returning %x\n", *(uint32_t *) data);
            }
        }
        else if((req->get_addr() & 0x17f) < 0x20) {
            *(uint32_t *) data = 0;
        }
        else {
            *(uint32_t *) data = _this->regfile_rd(((req->get_addr() & 0x17f) - 0x20) >> 2);
            if (trace_at_least(_this->trace_level, NEUREKA_L3_ALL)) {
                _this->trace.msg("Returning %x\n", *(uint32_t *) data);
            }
        }
    }

    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Neureka(config);
}
