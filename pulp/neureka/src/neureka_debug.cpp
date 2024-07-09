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
std::string Neureka::NeurekaStateToString(const NeurekaState& state) {
    switch (state) {
      case IDLE: return "IDLE";
      case START: return "START";
      case STREAMIN_SETUP: return "STREAMIN_SETUP";
      case STREAMIN: return "STREAMIN";
      case LOAD: return "LOAD";
      case LOAD_SETUP: return "LOAD_SETUP";
      case WEIGHTOFFSET: return "WEIGHTOFFSET";
      case WEIGHT_LOAD_SETUP: return "WEIGHT_LOAD_SETUP";
      case WEIGHT_LOAD: return "WEIGHT_LOAD";
      case PREFETCH_WEIGHT_LOAD_SETUP: return "PREFETCH_WEIGHT_LOAD_SETUP";
      case PREFETCH_WEIGHT_LOAD: return "PREFETCH_WEIGHT_LOAD";
      case UPDATE_IDX: return "UPDATE_IDX";
      case NORMQUANT_SHIFT_SETUP: return "NORMQUANT_SHIFT_SETUP";
      case NORMQUANT_SHIFT: return "NORMQUANT_SHIFT";
      case NORMQUANT_MULT_SETUP: return "NORMQUANT_MULT_SETUP";
      case NORMQUANT_MULT: return "NORMQUANT_MULT";
      case NORMQUANT_BIAS_SETUP: return "NORMQUANT_BIAS_SETUP";
      case NORMQUANT_BIAS: return "NORMQUANT_BIAS";
      case STREAMOUT_SETUP: return "STREAMOUT_SETUP";
      case STREAMOUT: return "STREAMOUT";
      case BEFORE_END: return "BEFORE_END";
      case END: return "END";
      default : 
        return "UNKNOWN";
    }
}

#define DEBUG_PRINT_NEUREKA
  void Neureka::printAccumAtIndex(int pe_index, int accum_index, const char* debug_msg){
    #ifdef DEBUG_PRINT
    OutFeatType accum = this->pe_instances[pe_index].ReadFromIndexAccumBuffer(accum_index);
    std::cout<<debug_msg<<" accum["<<pe_index<<"]["<<accum_index<<"]=0x"<<std::hex<<accum<<"\n";
    #endif
  }
  void Neureka::printAccumFullBuffer(int pe_index, const char* debug_msg){
    #ifdef DEBUG_PRINT
    xt::xarray<OutFeatType> accum = this->pe_instances[pe_index].ReadAllAccumBuffer();
    printXtensorXarray(accum, debug_msg);
    #endif
  }