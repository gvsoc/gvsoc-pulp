
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
#define L1BandwidthInBytes (256/8)
void Neureka::WeightOffset(int& latency) {

  if(this->reg_config_.config0.infeat_prefetch==true)
    this->infeat_dual_buffer_read_index = this->infeat_dual_buffer_read_index == 1 ? 0 : 1;
  else 
    this->infeat_dual_buffer_read_index = 1;
    
  Mode filter_mode = this->reg_config_.config0.filter_mode;
  std::array<std::array<std::array<bool,NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount>,NeurekaTotalPECountXY> compute_binconv_enable = this->ctrl_instance.ComputeBinconvEnable(true);

  // this->trace.msg("woffs start accum_0 %d\n", this->pe_instances[0].ReadFromIndexAccumBuffer(0));


  std::array<InFeatType, NeurekaColumnPerPECount> shift_array;
  std::fill(shift_array.begin(), shift_array.end(), 0);
  std::array<std::array<InFeatType, NeurekaBinConvPerColumnCount>, NeurekaColumnPerPECount> weight_array;

  // this->trace.msg("Woffs at buffer index %d \n", this->infeat_dual_buffer_read_index);

  this->infeat_buffer_instance.MapInFeatToEngines(this->infeat_dual_buffer_read_index, filter_mode);

  std::array<std::array<std::array<InFeatType,NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>,NeurekaTotalPECountXY> infeat_mapped_to_pe;
  infeat_mapped_to_pe = this->infeat_buffer_instance.ReadInFeatMappedToPE();
  // std::cout<<"infeat mapped tp pe"<<(int)infeat_mapped_to_pe[8][0][0]<<"\n";

  // for(int i=0; i<9; i++)
  // {
  //   this->trace.msg("infeat_mapped_to_pe[0][0][%d]:%d\n", i, infeat_mapped_to_pe[0][0][i]);
  // }
  bool is_signed = reg_config_.config0.signed_activation;
  std::array<bool,NeurekaAccumulatorPerPECount> accum_enable_array;

  std::fill(accum_enable_array.begin(), accum_enable_array.end(), true);
  std::array<OutFeatType,NeurekaAccumulatorPerPECount> psum_array;
  std::fill(psum_array.begin(), psum_array.end(), 0);
  OutFeatType psum_dense = 0;

  std::array<std::array<bool,NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount> psum_enable_array;
  if(filter_mode==Depthwise || filter_mode==Dense3x3 || filter_mode==Pointwise){
    for(int i=0; i<NeurekaTotalPECountXY; i++){
      for(int j=0; j<NeurekaInFeatScalarBufferCount; j++){
        std::fill(weight_array[j].begin(), weight_array[j].end(), 1);
      }
      psum_dense = 0;

      this->pe_instances[i].ComputePartialSum(filter_mode, compute_binconv_enable[i], infeat_mapped_to_pe[i], weight_array, shift_array, psum_array, psum_dense, is_signed);
      for (int j = 0; j < NeurekaAccumulatorPerPECount; ++j) {
        psum_array[j] = filter_mode==Depthwise ? psum_array[j] * this->reg_config_.Wmin : psum_dense * this->reg_config_.Wmin;
      }
      this->pe_instances[i].AccumulateAllAccumBuffer(accum_enable_array, psum_array);
      latency = this->reg_config_.Wmin==0 ? 0 : filter_mode==Depthwise ? DepthwiseWoffsetOverhead : DenseWoffsetOverhead;
    }
  }
  else 
    this->trace.fatal("Unsupported filter mode\n");
    
    if(this->reg_config_.config0.infeat_prefetch){
      this->adjust_weightoffset_cycles = latency - 6;
      if(this->adjust_weightoffset_cycles<0)
        this->adjust_weightoffset_cycles = 0;
      latency = 6;
    } else {
     this->adjust_weightoffset_cycles = 0;
   }

  // this->trace.msg("woffs start accum_0 %d\n", this->pe_instances[0].ReadFromIndexAccumBuffer(0));

  
}