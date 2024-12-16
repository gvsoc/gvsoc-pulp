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

#ifndef DATA_TYPE_H
#define DATA_TYPE_H
#define NeurekaInFeatType uint8_t
#define NeurekaOutFeatType int32_t // accumulator data type
#define InFeatType uint8_t
#define SignedInFeatType int8_t
#define WeightType uint8_t
#define OutFeatType int32_t // accumulator data type
#define AddrType uint32_t
#define StreamerDataType uint8_t
#define NormType uint32_t

#define WMEM_L1 0

class Neureka;
enum Mode
{
    Depthwise,
    Pointwise,
    Dense3x3
};


struct trace_enable_infeat{
  bool infeat_handler;
  bool matrix_buffer;
  bool linear_buffer;
  bool scalar_buffer;
};

struct trace_enable_outfeat{
  bool outfeat_handler;
  bool linear_buffer;
  bool scalar_buffer;
};

struct trace_enable_streamer{
  bool infeat;
  bool streamin; 
  bool outfeat;
  bool weight;
  bool norm_mult;
  bool norm_shift; 
  bool norm_bias;
};

template <typename T>
struct StateParams{
  T streamin;
  T woffset; 
  T update_idx;
  T infeat_load; 
  T weight_load; 
  T outfeat_store; 
  T norm_mult; 
  T norm_bias; 
  T norm_shift;
};
struct trace_enable {
  bool binconv;
  bool column;
  bool pe_compute;
  bool pe;
  trace_enable_infeat infeat_buffer;
  trace_enable_outfeat outfeat_buffer;
  StateParams<bool> streamer;
  StateParams<bool> setup;
  StateParams<bool> execute;
  bool fsm;
};
template <typename T>
struct Data3D
{
  T d0;
  T d1;
  T d2;
};

struct StreamerConfig{
  AddrType base_addr;
  Data3D<AddrType> stride;
  Data3D<AddrType> length;
};

struct Padding{
  InFeatType value; 
  int top;
  int bottom;
  int left;
  int right;
};

struct Config0{
  bool wait_for_infeat; // wait for input features to be available
  bool residual;
  bool signed_outfeat;
  bool signed_streamin;
  bool signed_activation;
  bool norm_option_bias;
  bool norm_option_shift;
  bool use_relu;
  bool streamin;
  bool infeat_prefetch;
  bool weight_from_wmem;
  bool strided2x2;
  bool outfeat_quant;
  bool depthwise_mode;
  bool is_signed;
  bool broadcast;
  int  filter_size;
  int  weight_bit_count;
  int  quantization_bit_count;
  int  quantization_right_shift;
  int  normalization_bit_count;
  int  streamin_bit_count;
  Mode filter_mode;
};
struct RegConfig{
  AddrType infeat_ptr;
  AddrType weight_ptr;
  AddrType outfeat_ptr;
  AddrType scale_ptr;
  AddrType scale_shift_ptr;
  AddrType scale_bias_ptr;
  AddrType streamin_ptr;
  Data3D<AddrType> infeat_stride;
  Data3D<AddrType> outfeat_stride;
  Data3D<AddrType> weight_stride;
  int wout_tile_count;
  int hout_tile_count;
  int kout_tile_count;
  int kin_tile_count;
  int wout_tile_rem;
  int hout_tile_rem;
  int win_tile_rem;
  int hin_tile_rem;
  int kout_tile_rem;
  int kin_tile_rem;
  Padding padding;
  int Wmin;
  bool filter_mask_bit[9];
  // int wgt_bits;
  // Mode mode;
  Config0 config0;
};

enum NeurekaState {
    IDLE,
    START,
    WAIT_FOR_INFEAT,
    START_STREAMIN,
    STREAMIN_LOAD,
    LOAD_WEIGHT_LOAD,
    WEIGHTOFFSET,
    STREAMIN_SETUP,
    STREAMIN,
    LOAD,
    LOAD_SETUP,
    WEIGHT_LOAD,
    WEIGHT_LOAD_SETUP,
    PREFETCH_WEIGHT_LOAD,
    PREFETCH_WEIGHT_LOAD_SETUP,
    NORMQUANT_SHIFT_SETUP,
    NORMQUANT_SHIFT,
    NORMQUANT_MULT_SETUP,
    NORMQUANT_MULT,
    NORMQUANT_BIAS_SETUP,
    NORMQUANT_BIAS,
    UPDATE_IDX,
    STREAMOUT_SETUP,
    STREAMOUT,
    BEFORE_END,
    END
};


enum NeurekaTraceLevel {
    L0_CONFIG,
    L1_ACTIV_INOUT,
    L2_DEBUG,
    L3_ALL
};

struct HwParams{
int FilterSize;
int ComputeRowCount; // 9 rows in the PE array
int WeightBitCount;
int AccumulatorPerPECount;// TP_OUT
int PECountX;
int PECountY;
int InFeatBufferSizeX;
int InFeatBufferSizeY;
int InFeatScalarBufferCount;// TP_IN channels
int Channelwise1x1;
int Repeated1x1;
int InFeatLinearBufferCount;//Total feature size 8x8
int TotalPECountXY;// Number of PEs 
int InFeatBufferInstanceCount; // For double buffering
int ColumnPerPECount;// Matches with TP_IN
int BinConvPerColumnCount;
};

template <typename T>
struct TilingParams{
  T kin;
  T kout;
  T hin;
  T hout;
  T win;
  T wout;
  T norm_quant_mult;
  T norm_quant_bias;
  T norm_quant_shift;
};

struct TilingStatus{
  TilingParams<int> count;
  TilingParams<int> index;
  TilingParams<bool> done;
  bool finish;
};

struct LoadInFeat{
  int hin;
  int win;
  int hinXwin;
};
struct LoadInFeatStatus{
  LoadInFeat count;
  LoadInFeat index;
  bool done;
};

struct LoadWeight{
  int  wgt;
  int  kout;
};

struct LoadWeightStatus{
  LoadWeight count;
  LoadWeight index;
  bool done;
};

struct StoreOutFeat{
  int  word;
  int  wout;
  int  hout;
};

struct StoreOutFeatStatus{
  StoreOutFeat count;
  StoreOutFeat index;
  bool done;
};

struct LoadStoreStatus{
  LoadInFeatStatus infeat;
  LoadWeightStatus weight;
  StoreOutFeatStatus outfeat;
  StoreOutFeatStatus streamin;
};

struct LayerPerformance{
  StateParams<int> num_mem_access;
  StateParams<int> num_state_cycles;
};

struct LayerProperties{
  int hout;
  int wout;
  int kin;
  int kout;
  int fs;
  int dw; 
  int local_layer_id;
  int residual;
  int strided;
  int broadcast;
  int norm_mult;
  int norm_shift; 
  int norm_bias;
};
struct FrameId{
  int sid; 
  int bid; 
  int nid;
  
  uint32_t arrival_time;
  uint32_t processing_start_time;
  uint32_t processing_end_time;
};

struct LayerSummary{
  LayerPerformance perf;
  LayerProperties  prop;
  FrameId frame;
};
#define N_PENDING_FRAMES 100 
#define N_SENSORS 10 

struct PendingFrames{
  std::array<int, N_PENDING_FRAMES> list;
  std::array<int, N_PENDING_FRAMES> arrival_time;
  std::array<int, N_SENSORS> received;
  std::array<int, N_SENSORS> processed;
  int num_elements;
};


#define L1BandwidthInBytes (256/8)
#define WmemBandwidthInBytes (288/8) // assuming full bandwidth at the moment 
#define L1_MASK 0x3ffff
// #define WMEM_MASK 0x007fffff
#define WMEM_MASK 0xFFFFFFFF
#endif
