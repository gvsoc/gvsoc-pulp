
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
std::string Neureka::NeurekaStateToString(const NeurekaState& state) {
    switch (state) {
      case IDLE: return "IDLE";
      case START: return "START";
      case STREAMIN_SETUP: return "STREAMIN_SETUP";
      case STREAMIN: return "STREAMIN";
      case WEIGHTOFFSET: return "WEIGHTOFFSET";
      case INFEAT_LOAD: return "INFEAT_LOAD";
      case INFEAT_LOAD_SETUP: return "INFEAT_LOAD_SETUP";
      case WEIGHT_LOAD: return "WEIGHT_LOAD";
      case WEIGHT_LOAD_SETUP: return "WEIGHT_LOAD_SETUP";
      case NORMQUANT_SHIFT_SETUP: return "NORMQUANT_SHIFT_SETUP";
      case NORMQUANT_MULT_SETUP: return "NORMQUANT_MULT_SETUP";
      case NORMQUANT_BIAS_SETUP: return "NORMQUANT_BIAS_SETUP";
      case NORMQUANT_SHIFT: return "NORMQUANT_SHIFT";
      case NORMQUANT_MULT: return "NORMQUANT_MULT";
      case NORMQUANT_BIAS: return "NORMQUANT_BIAS";
      case UPDATE_IDX: return "UPDATE_IDX";
      case STREAMOUT_SETUP: return "STREAMOUT_SETUP";
      case STREAMOUT: return "STREAMOUT";
      case BEFORE_END: return "BEFORE_END";
      case END: return "END";
      default : 
        return "UNKNOWN";
    }
}

#define DEBUG_PRINT_NEUREKA
#define DEBUG_PRINT