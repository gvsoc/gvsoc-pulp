
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
void Neureka::StreaminSetup() {
  this->ctrl_instance.ComputeDimensions();
  StreamerConfig streamer_config = this->ctrl_instance.GetStreaminStreamerConfig();
  this->streamin_streamer_instance.Init(streamer_config.base_addr, streamer_config.stride.d0, streamer_config.stride.d1, streamer_config.stride.d2, streamer_config.length.d0, streamer_config.length.d1, streamer_config.length.d2);
  this->ctrl_instance.ResetStreaminIteration();
  if(this->trace_config.setup.streamin)
    this->trace.msg("Stramin Setup is done addr : 0x%x, strides( d0 : 0x%x, d1 : 0x%x, d2 : 0x%x), lengths(d0 : %d, d1 : %d, d2 : %d)\n", streamer_config.base_addr, streamer_config.stride.d0, streamer_config.stride.d1, streamer_config.stride.d2, streamer_config.length.d0, streamer_config.length.d1, streamer_config.length.d2);
}

bool Neureka::StreaminExecute(int& latency)
{

  int width = this->ctrl_instance.StreaminLoadWidth();
  int pe_index = this->ctrl_instance.GetStreaminLinearBufferIndex();// which accumulator buffer to be used
  int word_index = this->ctrl_instance.GetStreaminWordIndex();

  uint64_t cycles = 0;
  std::array<StreamerDataType, L1BandwidthInBytes> streamin_data;
  std::fill(streamin_data.begin(), streamin_data.end(), 0);

  StreamerDataType streamin_data_temp[width];
  this->streamin_streamer_instance.VectorLoad(streamin_data_temp, width, cycles, this->trace_config.streamer.streamin);

  for(int i=0; i<width; i++)
    streamin_data[i] = streamin_data_temp[i];

  this->num_mem_access_bytes.streamin += width;
  latency = latency + (int)cycles ? latency + (int)cycles : 1 ;
  
  if(this->regconfig_manager_instance.reg_config_.config0.streamin_bit_count==32){
    for(int i=0; i<width/4; i++){
      OutFeatType data_32bit = ((streamin_data[4*i+3]<<24) + (streamin_data[4*i+2]<<16) + (streamin_data[4*i+1]<<8) + streamin_data[4*i]);
      this->pe_instances[pe_index].AccumulateAtIndexOnAccumBuffer(word_index+i, true, data_32bit);
    }
  } else if (this->regconfig_manager_instance.reg_config_.config0.streamin_bit_count==8) {
    for(int i=0; i<width; i++){
      int8_t data_signed_8bit = streamin_data[i];
      uint8_t data_unsigned_8bit = streamin_data[i];
      OutFeatType data_32bit = reg_config_.config0.signed_streamin ?  (OutFeatType)data_signed_8bit : (OutFeatType)data_unsigned_8bit;
      this->pe_instances[pe_index].AccumulateAtIndexOnAccumBuffer(word_index+i, true, data_32bit);
    }
  }
  else this->trace.fatal("Unsupported Quantization bit count \n");
  this->ctrl_instance.StreaminIteration();
 
  return this->ctrl_instance.load_store_status.streamin.done;
}
