
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
#ifndef NORMALIZATION_H
#define NORMALIZATION_H
#include "debug.hpp"
#include "datatype.hpp"
#include "params.hpp"

template <typename HwpeType, typename OutFeatType, typename NormType>
class NormalizationQuantizationUnit{
  private:
    RegConfig reg_config_;
    HwpeType* accel_instance_;
    int multiplier_count_=NeurekaNormMultiplierCount;
    int adder_count_=NeurekaNormAdderCount;
  public:
    NormalizationQuantizationUnit(){}
    NormalizationQuantizationUnit(HwpeType* accel, int NormMultiplierCount, int NormAdderCount){
      accel_instance_ = accel;
      multiplier_count_ = NormMultiplierCount;
      adder_count_ = NormAdderCount;
    }

    std::array<OutFeatType, 4>  NormQuantMult(const std::array<OutFeatType, 4>& input, const std::array<NormType, 4>& kappa, const std::array<NormType, 4>& shift){
      std::array<OutFeatType, 4> accum = {0};
      if(reg_config_.config0.normalization_bit_count==8){
        for(int i=0; i<4; i++)
        {
          accum[i] = kappa[i]*input[i];
          accum[i] = accum[i]>>shift[i];
        }
        return accum;
      } else if(reg_config_.config0.normalization_bit_count==16){
        for(int i=0; i<1; i++){
          accum[i] = kappa[i]*input[i];
          accum[i] = accum[i]>>shift[i];
        
        }
        return accum;
      } else if(reg_config_.config0.normalization_bit_count==32){
        for(int i=0; i<1; i++){
          accum[i] = kappa[i]*input[i];
          accum[i] = accum[i]>>shift[i];
        }
        return accum;
      }
      else{
        accel_instance_->trace.fatal("Unsupported mode\n");
      }
      return accum;
    }

    std::array<OutFeatType,8> NormQuantBias(const std::array<OutFeatType,8>& input, const std::array<NormType,8>& lambda){
      std::array<OutFeatType,8> accum;
      for(int i=0; i<adder_count_; i++)
        accum[i] = input[i] + lambda[i];
      return accum;
    }

    std::array<OutFeatType,8> NormQuantShift(const std::array<OutFeatType,8>& input, const std::array<NormType,8>& shift){
      std::array<OutFeatType,8> accum;
      for(int i=0; i<adder_count_; i++)
        accum[i] = input[i] >> shift[i];
      return accum;
    }

    std::array<OutFeatType,8> NormQuantBiasShift(const std::array<OutFeatType,8>& input,const std::array<NormType,8>& bias, const std::array<NormType,8>& shift){
        std::array<OutFeatType,8> accum;
        accum = NormQuantBias(input, bias);
        accum = NormQuantShift(accum, shift);
      return accum;
    }

    void SetConfig(RegConfig reg_config){reg_config_ = reg_config;}

};
#endif

      

