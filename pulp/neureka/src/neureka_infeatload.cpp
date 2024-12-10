
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
void Neureka::InFeatLoadSetup() {
  this->ctrl_instance.ComputeDimensions();
  if(this->reg_config_.config0.infeat_prefetch==true)
    this->infeat_dual_buffer_write_index = this->infeat_dual_buffer_write_index == 1 ? 0 : 1;
  else 
    this->infeat_dual_buffer_write_index = 1;
  StreamerConfig streamer_config = this->ctrl_instance.GetInFeatLoadStreamerConfig();
  this->infeat_streamer_instance.Init(streamer_config.base_addr, streamer_config.stride.d0, streamer_config.stride.d1, streamer_config.stride.d2, streamer_config.length.d0, streamer_config.length.d1, streamer_config.length.d2);
  this->ctrl_instance.ResetInFeatLoadIteration();
  if(this->trace_config.setup.infeat_load)
    this->trace.msg("InFeatLoad Setup is done addr : 0x%x, strides( d0 : 0x%x, d1 : 0x%x, d2 : 0x%x), lengths(d0 : %d, d1 : %d, d2 : %d)\n", streamer_config.base_addr, streamer_config.stride.d0, streamer_config.stride.d1, streamer_config.stride.d2, streamer_config.length.d0, streamer_config.length.d1, streamer_config.length.d2);
}

bool Neureka::InFeatLoadExecute(int& latency)
{
   if(this->ctrl_instance.prefetch_tiles.finish==true && this->reg_config_.config0.infeat_prefetch){
    latency = this->adjust_weightoffset_cycles;
    return true;
  }

  int width = L1BandwidthInBytes;
  uint64_t cycles = 0;
  std::array<bool, NeurekaInFeatScalarBufferCount> padding_enable;
  std::fill(padding_enable.begin(), padding_enable.end(), 0);
  padding_enable=this->ctrl_instance.GetPaddingEnable();
  this->ctrl_instance.InFeatLoadIteration();
  InFeatType infeat_data_temp[NeurekaInFeatScalarBufferCount];

  this->infeat_streamer_instance.VectorLoad(infeat_data_temp, width, cycles, this->trace_config.streamer.infeat_load);
  int access_width = reg_config_.config0.broadcast ? 1 : width;
  this->num_mem_access_bytes.infeat_load += access_width;

  latency = latency + (int)cycles ? latency + (int)cycles : 1 ;

  std::array<bool, NeurekaInFeatScalarBufferCount> enable;
  std::array<InFeatType, NeurekaInFeatScalarBufferCount> infeat_data;


  for(int i=0; i<width; i++){
    if(padding_enable[i]==true)
      infeat_data_temp[i] = this->reg_config_.padding.value;
    else if(reg_config_.config0.broadcast)
      infeat_data_temp[i] = infeat_data_temp[0];

    infeat_data[i] = infeat_data_temp[i];
    enable[i] = true; 
  }

  // for(int i=0; i<NeurekaInFeatScalarBufferCount; i++){
  //   this->trace.msg("enable[%d] : %d\n", i, enable[i]);
  // }

  // this->trace.msg("Load at buffer index %d \n", this->infeat_dual_buffer_write_index);
  int infeat_buffer_index = this->ctrl_instance.load_store_status.infeat.index.hinXwin;
  // this->trace.msg("infeat_buffer_index : %d\n", infeat_buffer_index);
  this->infeat_buffer_instance.WriteLinearBufferAtIndex(this->infeat_dual_buffer_write_index, infeat_buffer_index, enable, infeat_data);
  if(this->ctrl_instance.load_store_status.infeat.done)
    this->ctrl_instance.PrefetchCheckTileStatus();
  return this->ctrl_instance.load_store_status.infeat.done;
}
