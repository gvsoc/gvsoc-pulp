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
#ifndef MATRIX_BUFFER_H
#define MATRIX_BUFFER_H
#include "debug.hpp"
#include "datatype.hpp"
template <typename T, int ParamInFeatLinearBufferCount, int ParamInFeatScalarBufferCount>
/**
 * @brief This is a class handing Matrix buffer. 
 *
 * This buffer uses parametric number of Linear Buffer. 
 * @param ParamInFeatScalarBufferCount is number of ScalarBuffer per LinearBuffer 
 * @param ParamInFeatLinearBufferCount is number of LinearBuffer per MatrixBuffer. 
 * So total dimension of the buffer is ParamInFeatLinearBufferCount x ParamInFeatScalarBufferCount
 * @see LinearBuffer, ScalarBuffer
 */
class MatrixBuffer {

private:

/**
 * @brief number of linear buffers per Matrix Buffer. Initialized by the constructor of the class.
 * 
 * 
 */
/**
 * @brief number of scalar buffer per Linear Buffer. Initialized by the constructor of the class.
 * 
 */
/**
 * @private
 * 
 * @brief An object pointing to the array of Linear Buffer. 
 * 
 * A Matrix Buffer built using Linear buffer. This instantiates ParamInFeatLinearBufferCount number of LinearBuffer array.
 *
 * @tparam T The data type stored in the buffer array. 
 * 
 */
std::array<LinearBuffer<T,ParamInFeatScalarBufferCount>, ParamInFeatLinearBufferCount> linear_buffer_array_instance_;



public:
  /**
   * @brief It initializes individual linear_buffer_array_instance_ to LinearBuffer elements.
   * 
   * @param ParamInFeatLinearBufferCount The number of LinearBuffers.
   * @param ParamInFeatScalarBufferCount The number of LinearBuffer per Scalar Buffer.
   *
   */
  MatrixBuffer(){};

  /**
   * @brief Returns the number of Linear Buffer per Matrix Buffer
   * 
   * @return int 
   */

  int GetLinearBufferCount() const { return ParamInFeatLinearBufferCount; }
  /**
   * @brief Returns the number of Scalar Buffer per Linear Buffer
   * 
   * @return int 
   */
  int GetScalarBufferCount() const { return ParamInFeatScalarBufferCount; }


  void Write(const std::array<std::array<bool, ParamInFeatScalarBufferCount>, ParamInFeatLinearBufferCount>& enable, const std::array<std::array<T, ParamInFeatLinearBufferCount>, ParamInFeatScalarBufferCount>& data) {

    if(!( enable.size() == ParamInFeatLinearBufferCount  &&  data.size() == ParamInFeatLinearBufferCount  &&  enable[0].size() == ParamInFeatScalarBufferCount  &&  data[0].size() == ParamInFeatScalarBufferCount  ))
    {
      throw std::runtime_error("MATRIX_BUFFER Write- size does not match");
      
    } else {
      for(int i=0; i<ParamInFeatLinearBufferCount; i++)
        linear_buffer_array_instance_[i].Write(enable[i], data[i]);
    }
  }

  void WriteLinearBufferAtIndex(const int& linear_buffer_index, const std::array<bool, ParamInFeatScalarBufferCount>& enable, const std::array<T, ParamInFeatScalarBufferCount>& data) 
  {
    if(linear_buffer_index < ParamInFeatLinearBufferCount && data.size() == ParamInFeatScalarBufferCount && enable.size() == ParamInFeatScalarBufferCount)
      linear_buffer_array_instance_[linear_buffer_index].Write(enable, data); 
    else 
    { 
      throw std::runtime_error("MATRIX_BUFFER WriteLinearBufferAtIndex- size does not match");
    }
  }

  void WriteScalarBufferAtIndex(const int& linear_buffer_index, const int& scalar_buffer_index, const bool& enable, const T& value) {
    if(linear_buffer_index < ParamInFeatLinearBufferCount && scalar_buffer_index < ParamInFeatScalarBufferCount)
      linear_buffer_array_instance_[linear_buffer_index].WriteAtIndex(scalar_buffer_index, enable, value); 
    else 
      throw std::runtime_error("MATRIX BUFFER - Trying to access index beyond size");
  }

  
  void AccumulateScalarBufferAtIndex(const int& linear_buffer_index, const int& scalar_buffer_index, const bool& enable, const T& value) {
    if(linear_buffer_index < ParamInFeatLinearBufferCount && scalar_buffer_index < ParamInFeatScalarBufferCount)
      linear_buffer_array_instance_[linear_buffer_index].AccumulateAtIndex(scalar_buffer_index, enable, value); 
    else 
      throw std::runtime_error("MATRIX BUFFER - Trying to access index beyond size");
  }

  void Read(std::array<std::array<T, ParamInFeatScalarBufferCount>, ParamInFeatLinearBufferCount>& buffer_data) {
    for(int i=0; i<ParamInFeatLinearBufferCount; i++)
      for(int j=0; j<ParamInFeatScalarBufferCount; j++)
        buffer_data[i][j] = linear_buffer_array_instance_[i].ReadFromIndex(j);
  }

  T ReadScalarBufferFromIndex(const int& linear_buffer_index, const int& scalar_buffer_index) const {
    if( linear_buffer_index < ParamInFeatLinearBufferCount && scalar_buffer_index < ParamInFeatScalarBufferCount)
    {
      T buffer_data = linear_buffer_array_instance_[linear_buffer_index].ReadFromIndex(scalar_buffer_index);
      return buffer_data;
    }
    else 
      throw std::runtime_error("MATRIX BUFFER - Trying to access index beyond size");
  }

};
#endif // BUFFER_2D_H