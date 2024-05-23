
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
#ifndef PROCESSING_ENGINE_H
#define PROCESSING_ENGINE_H
#include "datatype.hpp"
#include "params.hpp"
/**
 * @brief Individual PE
 * Accumulator Units
 * PE Compute Unit
 * NormQuant Unit(not implemented yet)
 */
template <typename HwpeType, typename InFeatType, typename SignedInFeatType, typename OutFeatType, typename NormType>
class ProcessingEngine
{
  private: 
  /**
   * Accumulator is 1D buffer for PE 
  */
  LinearBuffer<OutFeatType, NeurekaInFeatScalarBufferCount> accum_buffer_; 
  /**
   * Single PE compute Unit per PE 
  */
  PEComputeUnit<HwpeType,InFeatType, SignedInFeatType, OutFeatType, NeurekaColumnPerPECount, NeurekaBinConvPerColumnCount> compute_unit_;
  NormalizationQuantizationUnit<HwpeType, OutFeatType, NormType> normquant_instance_;
  HwParams hw_param_;
  HwpeType* accel_instance_;
  std::array<OutFeatType, NeurekaColumnPerPECount> normquant_bias_, normquant_mult_;
  std::array<OutFeatType, NeurekaColumnPerPECount> normquant_shift_;


public:
  ProcessingEngine(){};
  ProcessingEngine(HwpeType* accel)
  { 
    accel_instance_ = accel;
  }

  std::array<OutFeatType, NeurekaInFeatScalarBufferCount> ReadAllAccumBuffer() const { return accum_buffer_.Read();}
  OutFeatType ReadFromIndexAccumBuffer(const int& index) const { return accum_buffer_.ReadFromIndex(index);};
  void WriteAtIndexOnAccumBuffer( const int& index, const bool& enable, const OutFeatType& value) { accum_buffer_.WriteAtIndex(index, enable, value); }
  void AccumulateAllAccumBuffer(const std::array<bool ,NeurekaColumnPerPECount>& enable, const std::array<OutFeatType, NeurekaColumnPerPECount>& value) { accum_buffer_.Accumulate(enable, value); };
  void AccumulateAtIndexOnAccumBuffer( const int& index, const bool& enable, const OutFeatType& value) { accum_buffer_.AccumulateAtIndex(index, enable, value);};
  void ComputePartialSum(Mode& mode, const std::array<std::array<bool, NeurekaBinConvPerColumnCount>, NeurekaColumnPerPECount>& enable, const std::array<std::array<InFeatType, NeurekaBinConvPerColumnCount>, NeurekaColumnPerPECount>& infeat, const std::array<std::array<InFeatType, NeurekaBinConvPerColumnCount>, NeurekaColumnPerPECount>& weight, const std::array<InFeatType, NeurekaColumnPerPECount>& shift, std::array<OutFeatType, NeurekaColumnPerPECount>&  sum_array, OutFeatType&  sum, const bool is_signed=false )
  { compute_unit_.ComputePartialSum(enable, infeat, weight, shift, sum_array, sum, is_signed);}

  void CalculatePartialSumAndUpdateAccumBuffer(Mode& mode, int index, const std::array<std::array<bool, NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount>& enable_binconv, const std::array<bool, NeurekaColumnPerPECount>& enable_accum, const std::array<std::array<InFeatType, NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount>& infeat, const std::array<std::array<InFeatType, NeurekaBinConvPerColumnCount>,NeurekaColumnPerPECount>& weight, const std::array<InFeatType, NeurekaColumnPerPECount>& shift, const bool is_signed=false)
  {
    OutFeatType sum=0;
    std::array<OutFeatType, NeurekaColumnPerPECount> sum_array;
    
    ComputePartialSum(mode, enable_binconv, infeat, weight, shift, sum_array, sum, is_signed);

    if(mode==Depthwise)// partial sum would of length AccumulatorPerPECount
      AccumulateAllAccumBuffer(enable_accum, sum_array);
    else if( mode==Pointwise || mode==Dense3x3 )
      if(index <NeurekaAccumulatorPerPECount){
        AccumulateAtIndexOnAccumBuffer(index, enable_accum[0], sum);
      }
      else 
        this->accel_instance_->trace.fatal("Trying to access Accumulator beyond size");
    else
      this->accel_instance_->trace.fatal("Trying to access Accumulator beyond size");
  }

  void SetConfig(RegConfig reg_config){normquant_instance_.SetConfig(reg_config);}

  void NormQuantMult(const std::array<int,4>& index, const std::array<int,4>& enable, const std::array<NormType,4>& kappa,  const std::array<NormType,4>& shift){
    int size = index.size();
    std::array<OutFeatType, 4> accum, accum_scaled;
    for(int i=0; i<size; i++)
      accum[i] = ReadFromIndexAccumBuffer(index[i]);
    accum_scaled = normquant_instance_.NormQuantMult(accum, kappa, shift);
    for(int i=0; i<size; i++)
      WriteAtIndexOnAccumBuffer( index[i], enable[i], accum_scaled[i]);
  }


  void NormQuantBiasShift(const std::array<int,8>& index, const std::array<int,8>& enable, const std::array<NormType,8>& bias, const std::array<NormType,8>& shift){
    int size = index.size();
    std::array<OutFeatType, 8> accum, accum_scaled;
    for(int i=0; i<size; i++)
      accum[i] = ReadFromIndexAccumBuffer(index[i]);
    accum_scaled = normquant_instance_.NormQuantBiasShift(accum, bias, shift);
    for(int i=0; i<size; i++){
      WriteAtIndexOnAccumBuffer(index[i], enable[i], accum_scaled[i]);
    }
  }

  void ResetAllAccumBuffer() { 
    std::array<bool,NeurekaColumnPerPECount> enable;
    std::fill(enable.begin(), enable.end(), true);
    std::array<OutFeatType,NeurekaColumnPerPECount> data;
    std::fill(data.begin(), data.end(), 0);
    accum_buffer_.Write(enable, data);  
  }

  void InitializeNormQuantMultBuffer(const std::array<OutFeatType, L1BandwidthInBytes>& mult, int width){
    for(int i=0; i<hw_param_.AccumulatorPerPECount; i++){
      normquant_mult_[i] = 0;
      if(i<width)
        normquant_mult_[i] = mult[i];
    }
  }

};
#endif