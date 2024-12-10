
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

void Neureka::WeightLoadSetup() {
  this->ctrl_instance.ComputeDimensions();
  if(this->reg_config_.Wmin==0){
    if(this->reg_config_.config0.infeat_prefetch==true)
      this->infeat_dual_buffer_read_index = this->infeat_dual_buffer_read_index == 1 ? 0 : 1;
    else 
      this->infeat_dual_buffer_read_index = 1;
  }

  StreamerConfig streamer_config = this->ctrl_instance.GetWeightLoadStreamerConfig();
  
  int bandwidth = this->reg_config_.config0.weight_from_wmem ? WmemBandwidthInBytes : L1BandwidthInBytes;
  
  this->weight_streamer_instance.Init(streamer_config.base_addr, streamer_config.stride.d0, streamer_config.stride.d1, streamer_config.stride.d2, streamer_config.length.d0, streamer_config.length.d1, streamer_config.length.d2);
  
  this->ctrl_instance.ResetWeightLoadIteration();
  // this->trace.msg("Weight load setup %d \n", this->infeat_dual_buffer_read_index);

  
  this->infeat_buffer_instance.MapInFeatToEngines(this->infeat_dual_buffer_read_index, this->reg_config_.config0.filter_mode);
  
  if(this->trace_config.setup.weight_load)
    this->trace.msg("WeightLoad Setup is done addr : 0x%x, strides( d0 : 0x%x, d1 : 0x%x, d2 : 0x%x), lengths(d0 : %d, d1 : %d, d2 : %d)\n", streamer_config.base_addr, streamer_config.stride.d0, streamer_config.stride.d1, streamer_config.stride.d2, streamer_config.length.d0, streamer_config.length.d1, streamer_config.length.d2);

}

void Neureka::WeightLoad(int& latency, std::array<StreamerDataType, WmemBandwidthInBytes>& weight) {
  int width = WmemBandwidthInBytes;
  // Load data using streamer
  int64_t cycles = 0;
  StreamerDataType weight_data_temp[width];
  if(reg_config_.config0.residual)  {

    for(int i=0; i<width; i++){
      weight_data_temp[i] = 0xff;
    }
  }
  else{
    this->weight_streamer_instance.VectorLoad(weight_data_temp, width, cycles, this->reg_config_.config0.weight_from_wmem, this->trace_config.streamer.weight_load);
    this->num_mem_access_bytes.weight_load += width;
    latency = (latency + (int)cycles) ? (latency + (int)cycles) : 1 ;
  }

  for(int i=0; i<width; i++){
    weight[i] = weight_data_temp[i];
  }
}

void WeightUnpack3x3(std::array<StreamerDataType, WmemBandwidthInBytes>& weight, std::array<std::array<uint8_t, NeurekaComputeRowCount>, NeurekaInFeatScalarBufferCount >& weight_unpacked) {
    // Reshape w
    for (size_t i = 0; i < NeurekaComputeRowCount; ++i) {
        for (size_t j = 0; j < (NeurekaInFeatScalarBufferCount/8); ++j) {
          for (size_t k = 0; k < 8; ++k) {
            weight_unpacked[j*8+k][i] = (weight[i*(NeurekaInFeatScalarBufferCount/8)+j] >> k) & 0x1; 
          }
        }
    }
}


void Neureka::WeightUnpack(Mode filter_mode,  std::array<StreamerDataType, WmemBandwidthInBytes>& weight, std::array<std::array<InFeatType, NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount>& weight_unpacked, int num_bits=NeurekaColumnPerPECount) {
  if(filter_mode != Pointwise){
    WeightUnpack3x3(weight, weight_unpacked);
    return;
  }
  
  for (int row = 0; row < NeurekaComputeRowCount; row++) 
    for(int byte = 0; byte < (NeurekaColumnPerPECount/8); byte++)
      for (int bit = 0; bit < 8; bit++){ 
          int index = byte*8 + bit;
          weight_unpacked[index][row] = (weight[index] >> row) & 0x1;
        }
}

void Neureka::Accumulate(const std::array<std::array<InFeatType, NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount>& weight_array) {
  
  
  std::array<std::array<std::array<bool,NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount>,NeurekaTotalPECountXY> compute_binconv_enable = this->ctrl_instance.ComputeBinconvEnable(false);
  std::array<std::array<std::array<InFeatType, NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>,NeurekaTotalPECountXY> infeat_mapped_to_pe = this->infeat_buffer_instance.ReadInFeatMappedToPE();
  std::array<bool, NeurekaTotalPECountXY> compute_pe_enable = this->ctrl_instance.ComputePEEnable();
  std::array<std::array<bool, NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> compute_col_enable;
  compute_col_enable = this->ctrl_instance.ComputeColumnEnable(false);
  bool is_signed = reg_config_.config0.signed_activation;
  std::array<InFeatType, NeurekaColumnPerPECount> shift_per_pe_array = {};
  int accum_index = 0;
  if(this->reg_config_.config0.filter_mode==Pointwise) { 
    std::array<InFeatType, NeurekaRepeated1x1> shift_values;
    for (int i = 0; i < NeurekaRepeated1x1; ++i) {
        shift_values[i] = i;
    }
    for(int i=0; i<NeurekaRepeated1x1; i++)
      for(int j=0; j<NeurekaChannelwise1x1; j++){
        shift_per_pe_array[i*NeurekaChannelwise1x1+j] = shift_values[i];
      }

    accum_index = this->ctrl_instance.GetWeightLoadKoutIndex(); // index to write into
  }
  else if(this->reg_config_.config0.filter_mode==Dense3x3 || this->reg_config_.config0.filter_mode==Depthwise){
    int weight_index = this->ctrl_instance.GetWeightLoadWeightIndex(); // index to write into
    for(int i=0; i<NeurekaColumnPerPECount; i++){
      shift_per_pe_array[i] = weight_index;
    }
    accum_index = this->ctrl_instance.GetWeightLoadKoutIndex();
  }

  std::array<bool, NeurekaColumnPerPECount> col_enable_array;
  std::array<bool, NeurekaColumnPerPECount> pe_col_enable_array;
  for(int i=0; i<NeurekaTotalPECountXY; i++) {
    for(int j=0; j<NeurekaColumnPerPECount; j++){
      col_enable_array[j] = compute_col_enable[i][j];
      pe_col_enable_array[j] = compute_pe_enable[i];
    }
    if(this->reg_config_.config0.filter_mode==Depthwise)
      this->pe_instances[i].CalculatePartialSumAndUpdateAccumBuffer(reg_config_.config0.filter_mode, accum_index, compute_binconv_enable[i], col_enable_array, infeat_mapped_to_pe[i], weight_array, shift_per_pe_array, is_signed);
    else
      this->pe_instances[i].CalculatePartialSumAndUpdateAccumBuffer(reg_config_.config0.filter_mode, accum_index, compute_binconv_enable[i], pe_col_enable_array, infeat_mapped_to_pe[i], weight_array, shift_per_pe_array, is_signed);
  }
  // this->trace.msg("mvec accum_0 %d\n", this->pe_instances[0].ReadFromIndexAccumBuffer(0));

}
bool Neureka::WeightLoadExecute(int& latency) {
    Mode filter_mode = this->reg_config_.config0.filter_mode;
    std::array<StreamerDataType,WmemBandwidthInBytes> weight_load_data = {};
    std::array<std::array<StreamerDataType,NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount> weight_unpacked = {};

    this->WeightLoad(latency, weight_load_data);
    this->WeightUnpack(filter_mode, weight_load_data, weight_unpacked);
    this->Accumulate(weight_unpacked);
    this->ctrl_instance.WeightLoadIteration();
    
    latency = latency ? latency : 1;
  
  return this->ctrl_instance.load_store_status.weight.done;
}
