/*
 * Copyright (C) 2020-2024  GreenWaves Technologies, ETH Zurich, University of Bologna
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
 * Authors: Arpan Suravi Prasad, ETH Zurich (prasadar@iis.ee.ethz.ch)
 */
#include "neureka.hpp"
#include <type_traits>

#include <string>
#include <sstream>
void Neureka::FsmStartHandler(vp::Block *__this, vp::ClockEvent *event) {// makes sense to move it to task manager
  Neureka *_this = (Neureka *)__this;
  _this->state.set(START);
  _this->regconfig_manager_instance.running_job_id_ = _this->regconfig_manager_instance.ctxt_job_id_[_this->regconfig_manager_instance.ctxt_use_ptr_];
  if(_this->trace_level == L3_ALL) {
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, " fsm >> start event\n");
  }
  // clear state and propagate context
  _this->ClearAll();
  _this->ctrl_instance.ResetIndexes();
  _this->reg_config_=_this->regconfig_manager_instance.RegfileCtxt();
  _this->regconfig_manager_instance.job_running_ = 1;

  if((_this->reg_config_.config0.weight_from_wmem == false) && (_this->reg_config_.config0.infeat_prefetch==true))
    _this->trace.fatal("Can't prefetch when wmem not in use %d  %d\n", _this->reg_config_.config0.weight_from_wmem, _this->reg_config_.config0.infeat_prefetch);
  _this->fsm_loop();
  
}
void Neureka::FsmHandler(vp::Block *__this, vp::ClockEvent *event) {
  Neureka *_this = (Neureka *)__this;
  if(_this->trace_level == L3_ALL) {
  }
  _this->fsm_loop();
}
void Neureka::FsmEndHandler(vp::Block *__this, vp::ClockEvent *event) {
  Neureka *_this = (Neureka *)__this;
  _this->trace.msg(vp::Trace::LEVEL_DEBUG,"END BEGINNING ctxt_job_id[0]=%d, ctxt_job_id[1]=%d, ctxt_use_ptr=%x, job_id=%d, job_pending_=%d\n",_this->regconfig_manager_instance.ctxt_job_id_[0], _this->regconfig_manager_instance.ctxt_job_id_[1], _this->regconfig_manager_instance.ctxt_use_ptr_, _this->regconfig_manager_instance.job_id_, _this->regconfig_manager_instance.job_pending_);
  int job_id = _this->regconfig_manager_instance.ctxt_job_id_[_this->regconfig_manager_instance.ctxt_use_ptr_];
  _this->regconfig_manager_instance.job_running_ = 0;
  _this->regconfig_manager_instance.ctxt_job_id_[_this->regconfig_manager_instance.ctxt_use_ptr_] = -1;
  _this->regconfig_manager_instance.ctxt_use_ptr_ = 1-_this->regconfig_manager_instance.ctxt_use_ptr_;
  _this->regconfig_manager_instance.job_pending_--;
  _this->irq.sync(true);
 
  _this->trace.msg(vp::Trace::LEVEL_INFO, "Ending job (id=%d).\n", job_id);
  _this->current_cycle = _this->fsm_start_event->get_cycle();
  if (!_this->fsm_start_event->is_enqueued() && _this->regconfig_manager_instance.job_pending_ > 0) {
      _this->fsm_start_event->enqueue(1);
      _this->trace.msg( "FSM Start Event enqueued with cycles=%d\n", _this->fsm_start_event->get_cycle());
      _this->trace.msg("ctxt_job_id[0]=%d, ctxt_job_id[1]=%d, ctxt_use_ptr=%x, job_id=%d\n",_this->regconfig_manager_instance.ctxt_job_id_[0], _this->regconfig_manager_instance.ctxt_job_id_[1], _this->regconfig_manager_instance.ctxt_use_ptr_, _this->regconfig_manager_instance.job_id_);
      _this->trace.msg("Starting a new job from the queue.\n");
  }
  _this->busy.set(0);
  _this->state.set(IDLE);
}
void Neureka::fsm_loop() {
  auto latency = 0;
  do {
    latency = this->fsm();
    this->current_cycle = this->fsm_event->get_cycle();
  } while(latency == 0 && state.get() != END);
  if(state.get() == END && !this->fsm_end_event->is_enqueued()) {
    this->fsm_end_event->enqueue(latency);
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "FSM End Event enqueued with cycles=%d\n", this->fsm_end_event->get_cycle());
    this->start_cycles = this->fsm_start_event->get_cycle();
    this->end_cycles = this->fsm_end_event->get_cycle();
    this->trace.msg("End of Computation\n");
    this->trace.msg("previous job took cycles : %d, start_cycles : %d, end_cycles : %d\n", (this->end_cycles - this->start_cycles), this->start_cycles, this->end_cycles);
    this->trace.msg("state wise cycles\r\n streamin : %d\r\n woffset : %d\r\n update_idx : %d\r\n infeat_load : %d\r\n weight_load : %d\r\n outfeat_store : %d\r\n norm_mult : %d\r\n norm_bias : %d\r\n norm_shift : %d\r\n\n", state_cycles.streamin, state_cycles.woffset, state_cycles.update_idx, state_cycles.infeat_load, state_cycles.weight_load, state_cycles.outfeat_store, state_cycles.norm_mult, state_cycles.norm_bias, state_cycles.norm_shift);
    this->current_cycle = this->end_cycles;

  }
  else if (!this->fsm_event->is_enqueued()) {
    if(this->trace_level == L3_ALL) {
      std::ostringstream stringStream;
      stringStream << "New Event Enqueued with latency = " <<latency<< "\n";
      std::string copyOfStr = stringStream.str();
    }
    this->fsm_event->enqueue(latency);
  }
}

int Neureka::fsm() {
  int latency = 0;
  int prefetch_latency = 0;
  bool load_done = false;
  bool weight_load_done = false;
  bool streamout_done = false;
  bool normquant_shift_done = false;
  bool normquant_mult_done = false;
  bool normquant_bias_done = false;
  bool streamin_done = false; 
  auto state_next = this->state.get();
  std::string state_string = NeurekaStateToString((NeurekaState)this->state.get());
  state_string = "(fsm state) current state "+ state_string + "\n"; 
  std::string debug_string;
  // std::cout<<state_string<<"\n";
   switch(this->state.get()) {
    case START:
      this->busy.set(1);
      std::cout<<"NID = "<<this->nid<<"\n";
      this->regconfig_manager_instance.PrintReg();
      this->ctrl_instance.SetConfig(regconfig_manager_instance.reg_config_);
      state_next = reg_config_.config0.streamin ? STREAMIN_SETUP : LOAD_SETUP;
      break;

    case STREAMIN_SETUP:
      StreaminSetup();
      state_next = STREAMIN;      
      break;
    case STREAMIN:
      streamin_done = StreaminExecute(latency);
      if(streamin_done){
        state_next = (this->first_infeat_load==false) || (this->reg_config_.config0.infeat_prefetch==false) ?  LOAD_SETUP : WEIGHTOFFSET;
        latency = latency+overhead.streamin;      }
      this->state_cycles.streamin += latency;
      break;
    case LOAD_SETUP:
      InFeatLoadSetup();
      state_next = LOAD;      
      break;
    case LOAD:
      this->first_infeat_load = true; 
      load_done = InFeatLoadExecute(latency);
      if(load_done){
        state_next = WEIGHTOFFSET;
        latency = latency+overhead.infeat_load;      }
      this->state_cycles.infeat_load += latency;
      break;
    case WEIGHTOFFSET : 
      WeightOffset(latency);
      state_next = (this->reg_config_.config0.infeat_prefetch==true) ?  PREFETCH_WEIGHT_LOAD_SETUP : WEIGHT_LOAD_SETUP;
      this->state_cycles.woffset += latency;
      break; 
    case WEIGHT_LOAD_SETUP:

      WeightLoadSetup();
      state_next = WEIGHT_LOAD;
      break;
    case WEIGHT_LOAD:
      weight_load_done = WeightLoadExecute(latency);
      if(weight_load_done){
        state_next = UPDATE_IDX; 
        latency = latency+overhead.weight_load;      }
      this->state_cycles.weight_load += latency;
      break;
    case PREFETCH_WEIGHT_LOAD_SETUP:
      this->ctrl_instance.PrefetchUpdateTileIndex();
      WeightLoadSetup();
      InFeatLoadSetup();
      this->debug_start_prefetch = this->fsm_event->get_cycle();
      state_next = PREFETCH_WEIGHT_LOAD;
      this->prefetch_weight_load_latency = 0;
      this->prefetch_infeat_load_latency = 0;      this->prefetch_weight_done = false ;
      this->prefetch_infeat_load_done = false ;
      break;
    case PREFETCH_WEIGHT_LOAD:
      if(this->prefetch_weight_done==false && this->adjust_weightoffset_cycles==0){
        if( this->prefetch_weight_load_latency==0){
          this->prefetch_weight_done = WeightLoadExecute(latency);
          this->prefetch_weight_load_latency += latency;
          latency = 1;
        }
      }
      if(this->prefetch_infeat_load_done==false){
        if(this->prefetch_infeat_load_latency == 0)
        {
          this->prefetch_infeat_load_done = InFeatLoadExecute(prefetch_latency);
          if(this->adjust_weightoffset_cycles > prefetch_latency){
            this->adjust_weightoffset_cycles  = this->adjust_weightoffset_cycles  - prefetch_latency; 
            latency = prefetch_latency;
            this->prefetch_infeat_load_latency = 0;
          } else if (prefetch_latency >= this->adjust_weightoffset_cycles){
            if(this->adjust_weightoffset_cycles != 0)
            {
              latency = this->adjust_weightoffset_cycles;
              this->prefetch_infeat_load_latency  += (prefetch_latency);
            } 
            else 
            {
              this->prefetch_infeat_load_latency += prefetch_latency;
            }
            this->adjust_weightoffset_cycles  = 0;
          }
          else if (this->adjust_weightoffset_cycles==0){
            this->prefetch_infeat_load_latency += prefetch_latency;
          }
        }
      }
      if(this->prefetch_weight_done && this->prefetch_infeat_load_done){
        state_next = UPDATE_IDX; 
        latency = overhead.weight_load + ((this->prefetch_infeat_load_latency - this->prefetch_weight_load_latency)>0 ? (this->prefetch_infeat_load_latency - this->prefetch_weight_load_latency) : (this->prefetch_weight_load_latency - this->prefetch_infeat_load_latency ) );
      } else if (this->prefetch_weight_done){
        latency = (this->prefetch_infeat_load_latency - this->prefetch_weight_load_latency) > 0? this->prefetch_infeat_load_latency : this->prefetch_weight_load_latency;
      }else if (this->prefetch_infeat_load_done){
        latency = (this->prefetch_weight_load_latency - this->prefetch_infeat_load_latency) > 0? this->prefetch_weight_load_latency : this->prefetch_infeat_load_latency;
        if(this->adjust_weightoffset_cycles > 0){
          latency = latency + this->adjust_weightoffset_cycles;
          this->adjust_weightoffset_cycles = 0;
        }
        this->prefetch_infeat_load_latency = 0;
        this->prefetch_weight_load_latency = 0;
      }
      if(latency==0){
        latency=1;
        this->prefetch_infeat_load_latency = this->prefetch_infeat_load_latency > 0 ? this->prefetch_infeat_load_latency-1 : 0;
        this->prefetch_weight_load_latency = this->prefetch_weight_load_latency > 0 ? this->prefetch_weight_load_latency-1 : 0;
      } else if(latency>0){
        this->prefetch_infeat_load_latency = this->prefetch_infeat_load_latency >= latency ? this->prefetch_infeat_load_latency-latency : 0;
        this->prefetch_weight_load_latency = this->prefetch_weight_load_latency >= latency ? this->prefetch_weight_load_latency-latency : 0;
      }
      if(this->prefetch_infeat_load_latency < 0) this->prefetch_infeat_load_latency = 0;
      if(this->prefetch_weight_load_latency < 0) this->prefetch_weight_load_latency = 0;
      if(latency>0)
        this->state_cycles.weight_load += latency;
      if(latency<0)
        this->trace.msg("latency is : %d\n", latency);
      break;
    case UPDATE_IDX:
      this->debug_stop_prefetch = this->fsm_event->get_cycle();
      this->ctrl_instance.CheckTileStatus();
      state_next = this->ctrl_instance.tiles.done.kin==false ? ((this->reg_config_.config0.infeat_prefetch==true)? WEIGHTOFFSET : LOAD_SETUP) : reg_config_.config0.outfeat_quant ? NORMQUANT_SHIFT_SETUP : STREAMOUT_SETUP;
      this->ctrl_instance.UpdateTileIndex();
      latency = overhead.update_idx;
      this->state_cycles.update_idx += latency;
      break;
    case NORMQUANT_SHIFT_SETUP:
      if(reg_config_.config0.norm_option_shift){
        NormQuantShiftSetup();
        state_next = NORMQUANT_SHIFT;
      } else {
        state_next = NORMQUANT_MULT_SETUP;
      }
      break;
    case NORMQUANT_SHIFT:
      normquant_shift_done = NormQuantShiftExecute(latency);
      state_next = normquant_shift_done ? NORMQUANT_MULT_SETUP : NORMQUANT_SHIFT;
      latency = normquant_shift_done ? latency+overhead.norm_shift : latency;
      this->state_cycles.norm_shift += latency;
      break;
    case NORMQUANT_MULT_SETUP:
      NormQuantMultSetup();
      state_next = NORMQUANT_MULT;
      break;
    case NORMQUANT_MULT:
      normquant_mult_done = NormQuantMultExecute(latency);
      state_next = normquant_mult_done==false ? NORMQUANT_MULT : reg_config_.config0.norm_option_bias ? NORMQUANT_BIAS_SETUP : STREAMOUT_SETUP;
      latency = normquant_mult_done ? latency+overhead.norm_mult : latency;
      this->state_cycles.norm_mult += latency;
      break;
    case NORMQUANT_BIAS_SETUP:
      NormQuantBiasSetup();
      state_next = NORMQUANT_BIAS;
      break;
    case NORMQUANT_BIAS:
      normquant_bias_done = NormQuantBiasExecute(latency);
      state_next = normquant_bias_done ? STREAMOUT_SETUP : NORMQUANT_BIAS;
      latency = normquant_bias_done ? latency+overhead.norm_bias : latency;
      this->state_cycles.norm_bias += latency;
      break;
    case STREAMOUT_SETUP:
      OutFeatStoreSetup();
      state_next = STREAMOUT;      
      break;
    case STREAMOUT:
      streamout_done = OutFeatStoreExecute(latency);
      if(streamout_done){
          this->debug_state_status.infeat_load = false;
          this->debug_state_status.streamin = false; 
          this->debug_state_status.outfeat_store = false;
          this->debug_state_status.weight_load = false;
          this->debug_state_status.norm_mult = false;
          this->debug_state_status.norm_shift = false; 
          this->debug_state_status.norm_bias = false;
        state_next = this->ctrl_instance.tiles.finish ?  END : reg_config_.config0.streamin ? STREAMIN_SETUP : (this->reg_config_.config0.infeat_prefetch==true ) ? WEIGHTOFFSET:LOAD_SETUP;
        latency = latency+overhead.outfeat_store;
      }
      this->state_cycles.outfeat_store += latency;
      break;
    case BEFORE_END:
        break;
    case END:
      break;
  }
  this->state.set(state_next);
  return latency;
}