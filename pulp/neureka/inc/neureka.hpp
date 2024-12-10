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
#include "debug.hpp"
#include "params.hpp"
#include "regconfig_manager.hpp"

#define N_LAYERS 10000


#include <array>
#include <cstdint> // For std::uint32_t


#define AGGREGATOR_MULTI_NEUREKA 1

template<typename T, size_t N>
void print1DArray(const std::array<T, N>& arr, int start, int end) {
    for (int i = start; i <= end && i < N; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << std::endl;
}

template<typename T, size_t N, size_t M>
void print2DArray(const std::array<std::array<T, M>, N>& arr, int rowStart, int rowEnd, int colStart, int colEnd) {
    for (int i = rowStart; i <= rowEnd && i < N; ++i) {
        for (int j = colStart; j <= colEnd && j < M; ++j) {
            std::cout << arr[i][j] << " ";
        }
        std::cout << std::endl;
    }
}

template<typename T, size_t N, size_t M, size_t L>
void print3DArray(const std::array<std::array<std::array<T, L>, M>, N>& arr, int depthStart, int depthEnd, int rowStart, int rowEnd, int colStart, int colEnd) {
    for (int k = depthStart; k <= depthEnd && k < N; ++k) {
        for (int i = rowStart; i <= rowEnd && i < M; ++i) {
            for (int j = colStart; j <= colEnd && j < L; ++j) {
                std::cout << arr[k][i][j] << " ";
            }
            std::cout << std::endl;
        }
        std::cout << "---" << std::endl; // Separator for each depth
    }
}

template<int DemuxSize, typename IndexType>
class IoMasterDemux : public vp::IoMaster {
public:
    IoMasterDemux() {};
    IoMasterDemux(std::array<vp::IoMaster*, DemuxSize> ports, IndexType* index) {
        this->ports = ports;
        this->index = index;
    }

    inline vp::IoReqStatus req(vp::IoReq* req) {
        assert(*index < ports.size() && "Out-of-bound index");
        return ports[*index]->req(req);
    }
private:
    std::array<vp::IoMaster*, DemuxSize> ports;
    IndexType* index;
};


class Neureka : public vp::Component
{
  friend class Neureka_base;

public:
  Neureka(vp::ComponentConf &config);

  int build();
  void reset(bool active);

//   static int layer_id; 

  vp::IoReq io_req;
  vp::IoMaster tcdm_port;
  vp::IoMaster wmem_tcdm_port;
  vp::IoMaster wmem_port;
  IoMasterDemux<2, bool> demux_port;

  StateParams<std::chrono::high_resolution_clock::time_point> cpu_cycles_start, cpu_cycles_end;
  StateParams<std::chrono::duration<double>> cpu_cycles_duration={};
  StateParams<int> num_mem_access_bytes; 
  
  vp::Trace trace;
  vp::reg_8 busy;
  vp::reg_32 state;
  NeurekaTraceLevel trace_level;
  int trace_format;
  HwParams hw_param_;
  RegConfig reg_config_;
  trace_enable trace_config;
  PendingFrames pending_frames;
  FrameId current_frame;
  uint32_t current_cycle;
  int local_layer_id = 0;

  int nid = 0;
  

private:
  vp::IoSlave cfg_port;
  static vp::IoReqStatus hwpe_slave(vp::Block *__this, vp::IoReq *req);
  vp::WireMaster<bool> irq;

  InFeatBuffer<InFeatType> infeat_buffer_instance;
  std::array<ProcessingEngine<Neureka, InFeatType, SignedInFeatType, OutFeatType, OutFeatType>, NeurekaTotalPECountXY> pe_instances; 
  Streamer<L1BandwidthInBytes, vp::IoMaster> infeat_streamer_instance;
  Streamer<L1BandwidthInBytes, vp::IoMaster> streamin_streamer_instance;
  Streamer<L1BandwidthInBytes, vp::IoMaster> outfeat_streamer_instance;
  Streamer<WmemBandwidthInBytes, IoMasterDemux<2, bool>> weight_streamer_instance;
  Streamer<L1BandwidthInBytes, vp::IoMaster> normquant_shift_streamer_instance;
  Streamer<L1BandwidthInBytes, vp::IoMaster> normquant_bias_streamer_instance;
  Streamer<L1BandwidthInBytes, vp::IoMaster> normquant_mult_streamer_instance;
  int infeat_dual_buffer_read_index, infeat_dual_buffer_write_index;

  // Streamer<AddrType, Neureka, StreamerDataType> normquant_streamer_instance;
  Control<Neureka> ctrl_instance;
  RegConfigManager<Neureka> regconfig_manager_instance;

  // Cycles<int> overhead( 3, 6, 2, 6, 6, 3, 8, 8, 7 );
  // Cycles<int> state_cycles(0, 0, 0, 0, 0, 0, 0, 0, 0); 
  StateParams<int> overhead, state_cycles;
  // Cycles<int> overhead, state_cycles;
  vp::ClockEvent *fsm_start_event;
  vp::ClockEvent *fsm_event;
  vp::ClockEvent *fsm_end_event;
  vp::ClockEvent *dummy_event;

  int start_cycles;
  int end_cycles;

  // EVENT handlers
  static void FsmStartHandler(vp::Block *__this, vp::ClockEvent *event);
  static void FsmHandler(vp::Block *__this, vp::ClockEvent *event);
  static void FsmEndHandler(vp::Block *__this, vp::ClockEvent *event);

  std::string NeurekaStateToString(const NeurekaState& state);

  bool first_infeat_load  = false;
  int prefetch_infeat_load_latency = 0;
  int prefetch_weight_load_latency = 0;
  bool prefetch_weight_done = false;
  bool prefetch_infeat_load_done = false;
  int adjust_weightoffset_cycles = 0; 

  int debug_start_prefetch, debug_stop_prefetch;

//   static LayerSummary network_summary[N_LAYERS];

  int  fsm();
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
  void printAccumAtIndex(int pe_index, int accum_index, const char* debug_msg);
  void printAccumFullBuffer(int pe_index, const char* debug_msg);
  void fsm_loop();

  StateParams<bool> debug_state_status;


  std::array<OutFeatType, NeurekaAccumulatorPerPECount> shift_values;



#endif /* __NEUREKA_HPP__ */

};

