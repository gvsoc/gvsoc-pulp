
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
#include <limits.h>
void Neureka::OutFeatStoreSetup() {
  StreamerConfig streamer_config = this->ctrl_instance.GetOutFeatStoreStreamerConfig();
  this->outfeat_streamer_instance.UpdateParams(streamer_config.base_addr, streamer_config.stride.d0, streamer_config.stride.d1, streamer_config.stride.d2, streamer_config.length.d0, streamer_config.length.d1, streamer_config.length.d2, L1BandwidthInBytes);
  this->ctrl_instance.ResetOutFeatStoreIteration();
  if(this->trace_config.setup.outfeat_store)
    this->trace.msg("OutFeatStore Setup is done addr : 0x%x, strides( d0 : 0x%x, d1 : 0x%x, d2 : 0x%x), lengths(d0 : %d, d1 : %d, d2 : %d)\n", streamer_config.base_addr, streamer_config.stride.d0, streamer_config.stride.d1, streamer_config.stride.d2, streamer_config.length.d0, streamer_config.length.d1, streamer_config.length.d2);
}
void Neureka::ResetAllAccumBuffer(){
  for(int i=0; i<NeurekaTotalPECountXY; i++)
    this->pe_instances[i].ResetAllAccumBuffer();
}
OutFeatType Neureka::OutFeatQuant(const OutFeatType input){
  OutFeatType output = input;
  if(reg_config_.config0.quantization_bit_count==8 && reg_config_.config0.outfeat_quant)
    if(reg_config_.config0.use_relu || reg_config_.config0.signed_outfeat==false){
      if(input < 0) output = 0;
      else if (input>255) output = 255;
      return output;
    }else{
      if(input < -128) output = -128;
      else if (input>127) output = 127;
      return output;
    }
  else if (reg_config_.config0.quantization_bit_count==32 && reg_config_.config0.outfeat_quant)
    if(reg_config_.config0.use_relu || reg_config_.config0.signed_outfeat==false){
      if(input < 0) output = 0;
      else if (input>0xffffffff) output = 0xffffffff;
      return output;
    }else{ 
       if(input < INT_MIN) output = INT_MIN;
      else if (input>0xffffffff) output = 0xffffffff;
      return output;
    }
  else 
    return output;
}
bool Neureka::OutFeatStoreExecute(int& latency)
{
  int width = this->ctrl_instance.OutFeatStoreWidth();
  int pe_index = this->ctrl_instance.GetOutFeatStoreLinearBufferIndex();// which accumulator buffer to be used
  int word_index = this->ctrl_instance.GetOutFeatStoreWordIndex();
  StreamerDataType store_data[L1BandwidthInBytes];

  // std::cout<<"pe_index="<<pe_index<<"\n";
  if(this->regconfig_manager_instance.reg_config_.config0.quantization_bit_count==32){
    for(int i=0; i<width/4; i++){
        OutFeatType temp_data = this->pe_instances[pe_index].ReadFromIndexAccumBuffer(word_index+i);
        OutFeatType data = OutFeatQuant(temp_data);
        for(int j=0; j<4; j++){
          StreamerDataType streamer_data = (data & (0xFF << 8*j)) >> (8*j);
          store_data[i*4+j] = streamer_data;
        }
    }
  } else if (this->regconfig_manager_instance.reg_config_.config0.quantization_bit_count==8) {
    for(int i=0; i<width; i++){
        OutFeatType temp_data = this->pe_instances[pe_index].ReadFromIndexAccumBuffer(i);
        OutFeatType data = OutFeatQuant(temp_data);
        store_data[i] = (StreamerDataType)data; 
    }
  }
  else this->trace.fatal("Unsupported Quantization bit count \n");

  
  int64_t cycles = 0;

  this->outfeat_streamer_instance.VectorStore(store_data, width, cycles, false, this->trace_config.streamer.outfeat_store);
  latency = latency + (int)cycles ? latency + (int)cycles : 1 ;
  this->num_mem_access_bytes.outfeat_store += width;
  
  this->ctrl_instance.OutFeatStoreIteration();
  bool streamout_done = this->ctrl_instance.load_store_status.outfeat.done;
  if(streamout_done){
    ResetAllAccumBuffer();
  }
 

  return streamout_done;
}
