
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
  _this->regconfig_manager_instance.RegfileCtxt();
  _this->regconfig_manager_instance.job_running_ = 1;
  _this->reg_config_=_this->regconfig_manager_instance.reg_config_;
  _this->fsm_loop();
}
void Neureka::FsmHandler(vp::Block *__this, vp::ClockEvent *event) {
  Neureka *_this = (Neureka *)__this;
  _this->fsm_loop();
}
void Neureka::FsmEndHandler(vp::Block *__this, vp::ClockEvent *event) {// makes sense to move it to task manager
  Neureka *_this = (Neureka *)__this;
  int job_id = _this->regconfig_manager_instance.ctxt_job_id_[_this->regconfig_manager_instance.ctxt_use_ptr_];
  _this->regconfig_manager_instance.job_running_ = 0;
  _this->regconfig_manager_instance.ctxt_job_id_[_this->regconfig_manager_instance.ctxt_use_ptr_] = -1;
  _this->regconfig_manager_instance.ctxt_use_ptr_ = 1-_this->regconfig_manager_instance.ctxt_use_ptr_;
  _this->regconfig_manager_instance.job_pending_--;
  _this->irq.sync(true);
 
  _this->trace.msg(vp::Trace::LEVEL_INFO, "Ending job (id=%d).\n", job_id);
  if (!_this->fsm_start_event->is_enqueued() && _this->regconfig_manager_instance.job_pending_ > 0) {
      _this->fsm_start_event->enqueue(1);
      _this->trace.msg( "FSM Start Event enqueued with cycles=%d\n", _this->fsm_start_event->get_cycle());
  }
  _this->busy.set(0);
  _this->state.set(IDLE);
}

void Neureka::fsm_loop() {
  auto latency = 0;
  do {
    latency = this->fsm();
  } while(latency == 0 && state.get() != END);
  if(state.get() == END && !this->fsm_end_event->is_enqueued()) {
    this->fsm_end_event->enqueue(latency);
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "FSM End Event enqueued with cycles=%d\n", this->fsm_end_event->get_cycle());
    this->start_cycles = this->fsm_start_event->get_cycle();
    this->end_cycles = this->fsm_end_event->get_cycle();
    this->trace.msg("previous job took cycles : %d, start_cycles : %d, end_cycles : %d\n", (this->end_cycles - this->start_cycles), this->start_cycles, this->end_cycles);
    this->trace.msg("state wise cycles\r\n streamin : %d\r\n woffset : %d\r\n update_idx : %d\r\n infeat_load : %d\r\n weight_load : %d\r\n outfeat_store : %d\r\n norm_mult : %d\r\n norm_bias : %d\r\n norm_shift : %d\r\n\n", state_cycles.streamin, state_cycles.woffset, state_cycles.update_idx, state_cycles.infeat_load, state_cycles.weight_load, state_cycles.outfeat_store, state_cycles.norm_mult, state_cycles.norm_bias, state_cycles.norm_shift);
  }
  else if (!this->fsm_event->is_enqueued()) {
    if(this->trace_level == L3_ALL) {
      std::ostringstream stringStream;
      stringStream << "New Event Enqueued with latency = " <<latency<< "\n";
      std::string copyOfStr = stringStream.str();
      this->trace.msg(vp::Trace::LEVEL_DEBUG, copyOfStr.c_str());
    }
    this->fsm_event->enqueue(latency);
    if(this->trace_level == L3_ALL) {
      this->trace.msg(vp::Trace::LEVEL_DEBUG, "FSM Event enqueued with cycles=%d\n", this->fsm_event->get_cycle());
    }
  }
}

int Neureka::fsm() {
  int latency = 0;
  bool load_done = false;
  bool weight_load_done = false;
  bool streamout_done = false;
  bool normquant_shift_done = false;
  bool normquant_mult_done = false;
  bool normquant_bias_done = false;
  bool streamin_done = false; 
  auto state_next = this->state.get();
   switch(this->state.get()) {
    case START:
      this->busy.set(1);
      this->regconfig_manager_instance.PrintReg();
      this->ctrl_instance.SetConfig(regconfig_manager_instance.reg_config_);
      state_next = reg_config_.config0.streamin ? STREAMIN_SETUP : INFEAT_LOAD_SETUP;
      break;
    case STREAMIN_SETUP:
      StreaminSetup();
      state_next = STREAMIN;
      break;
    case STREAMIN:
      streamin_done = StreaminExecute(latency);
      if(streamin_done){
        state_next = INFEAT_LOAD_SETUP;
        latency = latency+overhead.streamin;
      }
      state_cycles.streamin += latency;
      break;
    case INFEAT_LOAD_SETUP:
      InFeatLoadSetup();
      state_next = INFEAT_LOAD;
      break;
    case INFEAT_LOAD:
      load_done = InFeatLoadExecute(latency);
      if(load_done){
        state_next = WEIGHTOFFSET;
        latency = latency+overhead.infeat_load;
      }
      state_cycles.infeat_load += latency;
      break;
    case WEIGHTOFFSET : 
      WeightOffset(latency);
      state_next = WEIGHT_LOAD_SETUP;
      state_cycles.woffset += latency;
      break; 
    case WEIGHT_LOAD_SETUP:
      WeightLoadSetup();
      state_next = WEIGHT_LOAD;
      break;
    case WEIGHT_LOAD:
      weight_load_done = WeightLoadExecute(latency);
      if(weight_load_done){
        state_next = UPDATE_IDX; 
        latency = latency+overhead.weight_load;
      }
      state_cycles.weight_load += latency;
      break;
    case UPDATE_IDX:
      this->ctrl_instance.CheckTileStatus();
      state_next = this->ctrl_instance.tiles.done.kin==false ? INFEAT_LOAD_SETUP : reg_config_.config0.outfeat_quant ? NORMQUANT_SHIFT_SETUP : STREAMOUT_SETUP;
      this->ctrl_instance.UpdateTileIndex();
      latency = overhead.update_idx;
      state_cycles.update_idx += latency;
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
      state_cycles.norm_shift += latency;
      break;
    case NORMQUANT_MULT_SETUP:
      NormQuantMultSetup();
      state_next = NORMQUANT_MULT;
      break;
    case NORMQUANT_MULT:
      normquant_mult_done = NormQuantMultExecute(latency);
      state_next = normquant_mult_done==false ? NORMQUANT_MULT : reg_config_.config0.norm_option_bias ? NORMQUANT_BIAS_SETUP : STREAMOUT_SETUP;
      latency = normquant_mult_done ? latency+overhead.norm_mult : latency;
      state_cycles.norm_mult += latency;
      break;
    case NORMQUANT_BIAS_SETUP:
      NormQuantBiasSetup();
      state_next = NORMQUANT_BIAS;
      break;
    case NORMQUANT_BIAS:
      normquant_bias_done = NormQuantBiasExecute(latency);
      state_next = normquant_bias_done ? STREAMOUT_SETUP : NORMQUANT_BIAS;
      latency = normquant_bias_done ? latency+overhead.norm_bias : latency;
      state_cycles.norm_bias += latency;
      break;
    case STREAMOUT_SETUP:
      OutFeatStoreSetup();
      state_next = STREAMOUT;
      break;
    case STREAMOUT:
      streamout_done = OutFeatStoreExecute(latency);
      if(streamout_done){
        state_next = this->ctrl_instance.tiles.finish ?  END : reg_config_.config0.streamin ? STREAMIN_SETUP : INFEAT_LOAD_SETUP;
        latency = latency+overhead.outfeat_store;

      }
      state_cycles.outfeat_store += latency;
      break;
    case BEFORE_END:
        break;

    case END:
      break;

  }

  std::string state_string = NeurekaStateToString((NeurekaState)this->state.get());
  if(this->trace_config.fsm)
  {
    state_string = "(fsm state) current state "+ state_string + " finished with latency : "+std::to_string(latency)+" cycles\n"; 
    this->trace.msg(state_string.c_str());
  }
  this->state.set(state_next);
  return latency;
}