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

#ifndef __NEUREKA_HPP__
#define __NEUREKA_HPP__
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <assert.h>
#include <string>
#include <bitset>
#include "binconv.hpp"
#include "column.hpp"
#include "pe_compute_unit.hpp"
#include "normalization.hpp"
#include "scalar_buffer.hpp"
#include "linear_buffer.hpp"
#include "matrix_buffer.hpp"
#include "streamer.hpp"
#include "control.hpp"
#include "infeat_handler.hpp"
#include "processing_engine.hpp"
#include "regconfig_manager.hpp"
class Neureka : public vp::Component
{
  friend class Neureka_base;

public:
  Neureka(vp::ComponentConf &config);

  int build();
  void reset(bool active);

  vp::IoReq io_req;
  vp::IoMaster tcdm_port;
  vp::IoMaster wmem_port;
  
  vp::Trace trace;
  vp::reg_8 busy;
  vp::reg_32 state;
  NeurekaTraceLevel trace_level;
  int trace_format;
  HwParams hw_param_;
  RegConfig reg_config_;
  TraceConfig trace_config;
  
private:
  vp::IoSlave cfg_port;
  static vp::IoReqStatus hwpe_slave(vp::Block *__this, vp::IoReq *req);
  vp::WireMaster<bool> irq;

  InFeatBuffer<InFeatType> infeat_buffer_instance;
  std::array<ProcessingEngine<Neureka, InFeatType, SignedInFeatType, OutFeatType, OutFeatType>, NeurekaTotalPECountXY> pe_instances; 
  Streamer<AddrType, Neureka, InFeatType, L1BandwidthInBytes> infeat_streamer_instance;
  Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes> streamin_streamer_instance;
  Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes> outfeat_streamer_instance;
  Streamer<AddrType, Neureka, StreamerDataType, WmemBandwidthInBytes> weight_streamer_instance;
  Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes> normquant_shift_streamer_instance;
  Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes> normquant_bias_streamer_instance;
  Streamer<AddrType, Neureka, StreamerDataType, L1BandwidthInBytes> normquant_mult_streamer_instance;



  Control<Neureka> ctrl_instance;
  RegConfigManager<Neureka> regconfig_manager_instance;

  StateParams<int> overhead, state_cycles;
  vp::ClockEvent *fsm_start_event;
  vp::ClockEvent *fsm_event;
  vp::ClockEvent *fsm_end_event;
  vp::ClockEvent *frame_event;

  int start_cycles;
  int end_cycles;

  // EVENT handlers
  static void FsmStartHandler(vp::Block *__this, vp::ClockEvent *event);
  static void FsmHandler(vp::Block *__this, vp::ClockEvent *event);
  static void FsmEndHandler(vp::Block *__this, vp::ClockEvent *event);

  std::string NeurekaStateToString(const NeurekaState& state);

  int  fsm();
  void fsm_loop();
  void ClearAll();
  void InFeatLoadSetup();
  bool InFeatLoadExecute(int& latency);
  void WeightOffset(int& latency);


  bool StreaminExecute(int& latency);
  void StreaminSetup();

  void WeightLoadSetup();
  bool WeightLoadExecute(int& latency);
  void WeightLoad(int& latency, std::array<StreamerDataType, WmemBandwidthInBytes>& weight);
  void WeightUnpack(Mode filter_mode,  std::array<StreamerDataType, WmemBandwidthInBytes>& weight, std::array<std::array<InFeatType, NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount>& weight_unpacked, int num_bits);
  void Accumulate(const std::array<std::array<InFeatType, NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount>& weight_array);

  bool OutFeatStoreExecute(int& latency);
  void OutFeatStoreSetup();
  void ResetAllAccumBuffer();

  void NormQuantShiftSetup();
  void NormQuantMultSetup();
  void NormQuantBiasSetup();

  bool NormQuantShiftExecute(int& latency);
  bool NormQuantMultExecute(int& latency);
  bool NormQuantBiasExecute(int& latency);
  OutFeatType OutFeatQuant(const OutFeatType input);
  std::array<OutFeatType, NeurekaAccumulatorPerPECount> shift_values;

};

#endif /* __NEUREKA_HPP__ */


