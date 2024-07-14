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

#define NEUREKA_REGISTER_CXT0_OFFS 0x90
#define NEUREKA_REGISTER_CXT1_OFFS 0x120
#define NEUREKA_REGISTER_OFFS 0x00
#include "neureka.hpp"

using namespace std::placeholders;

Neureka::Neureka(vp::ComponentConf &config)
    : vp::Component(config)
{
  for(int i=0; i<NeurekaTotalPECountXY; i++)
    this->pe_instances[i] = ProcessingEngine<Neureka, InFeatType, SignedInFeatType, OutFeatType, OutFeatType>(this);
  
  this->ctrl_instance = Control<Neureka>(this);
  this->regconfig_manager_instance = RegConfigManager<Neureka>(this, NeurekaRegisterContextCount, NEUREKA_NB_REG);
  this->infeat_streamer_instance = Streamer<AddrType, Neureka, InFeatType, L1BandwidthInBytes>(this,  0, 0, 0, 0, 0, 0, 0,32, 4);
  this->streamin_streamer_instance = Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes>(this,  0, 0, 0, 0, 0, 0, 0,32, 4);
  this->outfeat_streamer_instance = Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes>(this,  0, 0, 0, 0, 0, 0, 0,32, 4);
  this->weight_streamer_instance = Streamer<AddrType, Neureka, StreamerDataType, WmemBandwidthInBytes>(this,  0, 0, 0, 0, 0, 0, 0,32, 4);
  this->normquant_shift_streamer_instance=Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes>(this,  0, 0, 0, 0, 0, 0, 0,32, 4);
  this->normquant_bias_streamer_instance=Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes>(this,  0, 0, 0, 0, 0, 0, 0,32, 4);
  this->normquant_mult_streamer_instance=Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes>(this,  0, 0, 0, 0, 0, 0, 0,32, 4);

  this->traces.new_trace("trace", &this->trace, vp::DEBUG);
  this->trace_level = L0_CONFIG;
  this->trace_format = 0;

  this->new_reg("fsm_state", &this->state, 32);//FSM state register
  this->new_reg("neureka_busy", &this->busy, 8);//Busy is high when neureka is operating
  this->busy.set(0);
  this->state.set(IDLE);

  this->new_master_port("tcdm_port", &this->tcdm_port );
  this->new_master_port("wmem_port", &this->wmem_port );
  this->new_master_port("irq", &this->irq);
  
  this->cfg_port.set_req_meth(&Neureka::hwpe_slave);
  this->new_slave_port("input", &this->cfg_port); 

  this->fsm_start_event = this->event_new(&Neureka::FsmStartHandler);
  this->fsm_event = this->event_new(&Neureka::FsmHandler);
  this->fsm_end_event = this->event_new(&Neureka::FsmEndHandler);
 }
void Neureka::reset(bool active)
{
  this->regconfig_manager_instance.ctxt_job_id_[0] = this->regconfig_manager_instance.ctxt_job_id_[1] = -1;
  this->regconfig_manager_instance.job_id_          = 0;
  this->regconfig_manager_instance.running_job_id_  = 0;
  this->regconfig_manager_instance.job_running_     = 0;
  this->regconfig_manager_instance.ctxt_cfg_ptr_    = 0x00;
  this->regconfig_manager_instance.ctxt_use_ptr_    = 0x0;
  this->regconfig_manager_instance.job_pending_     = 0x0;
  this->overhead.streamin=3;
  this->overhead.woffset=6; 
  this->overhead.update_idx=2;
  this->overhead.infeat_load=6; 
  this->overhead.weight_load=6; 
  this->overhead.outfeat_store=3; 
  this->overhead.norm_mult=8; 
  this->overhead.norm_bias=8; 
  this->overhead.norm_shift=7;

  this->state_cycles.infeat_load = 0;
  this->state_cycles.streamin = 0;
  this->state_cycles.outfeat_store = 0;
  this->state_cycles.weight_load = 0;
  this->state_cycles.woffset = 0;
  this->state_cycles.norm_mult = 0;
  this->state_cycles.norm_shift = 0;
  this->state_cycles.norm_bias = 0;

  // Task 4 - Enable trace for regconfig
  this->trace_config.regconfig = false;

  // Task 5 - Enable trace for fsm
  this->trace_config.fsm = false;

  // Task 6 - Enable trace for infeat load streamer
  this->trace_config.streamer.infeat_load = false;

  this->trace_config.streamer.streamin = false; 
  this->trace_config.streamer.outfeat_store = false;
  this->trace_config.streamer.weight_load = false;
  this->trace_config.streamer.norm_mult = false;
  this->trace_config.streamer.norm_shift = false; 
  this->trace_config.streamer.norm_bias = false;

  this->trace_config.setup.infeat_load = false;
  this->trace_config.setup.streamin = false; 
  this->trace_config.setup.outfeat_store = false;
  this->trace_config.setup.weight_load = false;
  this->trace_config.setup.norm_mult = false;
  this->trace_config.setup.norm_shift = false; 
  this->trace_config.setup.norm_bias = false;

  this->trace_config.execute.infeat_load = false;
  this->trace_config.execute.streamin = false; 
  this->trace_config.execute.outfeat_store = false;
  this->trace_config.execute.weight_load = false;
  this->trace_config.execute.norm_mult = false;
  this->trace_config.execute.norm_shift = false; 
  this->trace_config.execute.norm_bias = false;
}
void Neureka::ClearAll()
{
  this->state_cycles.infeat_load = 0;
  this->state_cycles.streamin = 0;
  this->state_cycles.outfeat_store = 0;
  this->state_cycles.weight_load = 0;
  this->state_cycles.norm_mult = 0;
  this->state_cycles.norm_shift = 0;
  this->state_cycles.norm_bias = 0;
}
// The `hwpe_slave` member function models an access to the NEUREKA SLAVE interface
vp::IoReqStatus Neureka::hwpe_slave(vp::Block *__this, vp::IoReq *req)
{
    Neureka *_this = (Neureka *)__this;

    if(_this->trace_config.regconfig)
        _this->trace.msg("Received request (addr: 0x%x, size: 0x%x, is_write: %d, data: %d\n", req->get_addr(), req->get_size(), req->get_is_write(), *(uint32_t *)(req->get_data()));

    uint8_t *data = req->get_data(); 
    uint32_t addr = req->get_addr();
    uint32_t addr_without_offset;

    if(addr > NEUREKA_REGISTER_CXT1_OFFS)
        addr_without_offset = addr - NEUREKA_REGISTER_CXT1_OFFS;
    else if(addr > NEUREKA_REGISTER_CXT0_OFFS)
        addr_without_offset = addr - NEUREKA_REGISTER_CXT0_OFFS;
    else 
        addr_without_offset = addr - NEUREKA_REGISTER_OFFS;


    // Dispatch the register file access to the correct function
    if(req->get_is_write()) {
        if(((addr_without_offset & 0xfff) - 0x20) >> 2 == NEUREKA_SPECIAL_TRACE_REG) {
            if(*data == 0) {
                _this->trace_level = L0_CONFIG;
                _this->trace.msg("Setting tracing level to L0_CONFIG\n");
            }
            else if(*data == 1) {
                _this->trace_level = L1_ACTIV_INOUT;
                _this->trace.msg("Setting tracing level to L1_ACTIV_INOUT\n");
            }
            else if(*data == 2) {
                _this->trace_level = L2_DEBUG;
                _this->trace.msg("Setting tracing level to L2_DEBUG\n");
            }
            else {
                _this->trace_level = L3_ALL;
                _this->trace.msg("Setting tracing level to L3_ALL\n");
            }
            return vp::IO_REQ_OK;
        }
        else if((addr_without_offset - 0x20) >> 2 == NEUREKA_SPECIAL_FORMAT_TRACE_REG) {
            _this->trace_format = *data;
            _this->trace.msg("Setting tracing format to %s\n", *data?"Hex":"Dec");
            return vp::IO_REQ_OK;
        }
        else if(addr_without_offset == 0x0) {
            // A Job is commited
            _this->regconfig_manager_instance.Commit();
            if (!_this->regconfig_manager_instance.job_running_ && !_this->fsm_start_event->is_enqueued() && *(uint32_t *) data == 0) {
                // First evenet is enqueued with latency=1 and the call back method is _this->fsm_start_event
                _this->fsm_start_event->enqueue(1);
                if(_this->trace_config.regconfig)
                    _this->trace.msg("********************* First event enqueued *********************\n");
            }
        }
        else {
            if(_this->trace_config.regconfig)
              _this->trace.msg("Setting Register offset: %d data: %08x\n", (addr_without_offset - 0x20) >> 2, *(uint32_t *) data);
            _this->regconfig_manager_instance.RegfileWrite((addr_without_offset - 0x20)>> 2, *(uint32_t *) data);
        }
    }
    else {
        if(addr_without_offset == 0x4) {
            *(uint32_t *) data = _this->regconfig_manager_instance.Acquire();
            if(_this->trace_config.regconfig) {
                _this->trace.msg("Returning %x\n", *(uint32_t *) data);
            }
        }
        else if(addr_without_offset == 0xc) {
            *(uint32_t *) data = ((_this->regconfig_manager_instance.ctxt_job_id_[0]>=0?0x1:0)|(_this->regconfig_manager_instance.ctxt_job_id_[1]>=0?0x100:0));
            if(_this->trace_config.regconfig)
            {
                _this->trace.msg("ctxt_job_id_[0] %x\n", _this->regconfig_manager_instance.ctxt_job_id_[0]);
                _this->trace.msg("ctxt_job_id_[1] %x\n", _this->regconfig_manager_instance.ctxt_job_id_[1]);
                _this->trace.msg("Returning %x\n", *(uint32_t *) data);
            }
        }
        else if(addr_without_offset == 0x10) {
            *(uint32_t *) data = _this->regconfig_manager_instance.running_job_id_;
            if(_this->trace_config.regconfig)
            {
                _this->trace.msg("Returning job id %x\n", *(uint32_t *) data);
            }
        }
        else {
            *(uint32_t *) data = _this->regconfig_manager_instance.RegfileRead((addr_without_offset - 0x20) >> 2);
            if(_this->trace_config.regconfig)
            {
                _this->trace.msg("Reading Register :  %x\n", *(uint32_t *) data);
            }
        }
    }

    return vp::IO_REQ_OK;
}

int Neureka::build()
{
 
  return 0;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Neureka(config);
}
