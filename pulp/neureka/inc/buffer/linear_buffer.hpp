
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
#ifndef LINEAR_BUFFER_H
#define LINEAR_BUFFER_H
#include "debug.hpp"
#include "datatype.hpp"
template <typename T, int ParamInFeatScalarBufferCount>
/**
 * @brief This is a class handing 1-D buffer. 
 *
 * This buffer uses parametric number of 0-D buffer. Check buffer_0D documentation for infor regarding buffer_0D.   
 */
class LinearBuffer {
private:
  /**
   * @brief data_ is the local Linear Buffer of length ParamInFeatScalarBufferCount
   * 
   */
    std::array<ScalarBuffer<T>, ParamInFeatScalarBufferCount> data_ ;
    // int ParamInFeatScalarBufferCount;
public:
  /**
   * @brief Constructor. It initializes the all the buffer content to 0.
   *
   */
  LinearBuffer(){};
  /**
   * @brief Get the Size of the Linear Buffer
   * 
   * @return int 
   */
  int GetSize() const {
      return ParamInFeatScalarBufferCount;
  }

   /**
   * @brief ReadFromIndex reads the content of a single ScalarBuffer given by index
   * 
   * @param index parameter is used to read that particular buffer
   * @return value stores in the ScalarBuffer indexed by index in the LinearBuffer
   */
  T ReadFromIndex(const int& index) const {
    if(index < ParamInFeatScalarBufferCount)
      return data_[index].Read();
    else 
      throw std::runtime_error("LINEAR_BUFFER ReadFromIndex - Trying to access index beyond size");

  }

  /**
   * @brief WriteAtIndex writes value to ScalarBuffer
   * 
   * Write to the buffer is followed by update to the localbuffer to keep it in sync with the upadted value
   * @param index parameter is used to write that particular buffer
   * @param enable when enable is true, the buffer content is updated
   * @param value data to be written to the buffer
   */

  void WriteAtIndex( const int& index, const bool& enable, const T& value) {
    if(index < data_.size())
      data_[index].Write(enable, value);
    else 
      throw std::runtime_error("LINEAR_BUFFER WriteAtIndex- Trying to access index beyond size");
  }

   /**
   * @brief AccumulateAtIndex accumulates value to ScalarBuffer
   * 
   * Accumulation to the buffer is followed by update to the localbuffer to keep it in sync with the upadted value
   * @param index index of the buffer to be accumulated
   * @param enable when enable is true, the buffer content is updated
   * @param value data to be accumulated on the top of existing data to the buffer
   */
  void AccumulateAtIndex( const int& index, const bool& enable, const T& value) {
    if(index < data_.size())
      data_[index].Accumulate(enable, value);
    else
      throw std::runtime_error("LINEAR_BUFFER AccumulateAtIndex- Trying to access index beyond size");
  }


  void Write( const std::array<bool, ParamInFeatScalarBufferCount>& enable, const std::array<T, ParamInFeatScalarBufferCount>& value ) {
    if(!(value.size() == ParamInFeatScalarBufferCount && enable.size() == ParamInFeatScalarBufferCount))
    {
    }
    else
    {
      for(int i=0; i<ParamInFeatScalarBufferCount; i++)
        if(enable[i]==true) data_[i].Write(enable[i], value[i]);
    }
  }

  void Accumulate( const std::array<bool,ParamInFeatScalarBufferCount> enable, const std::array<T, ParamInFeatScalarBufferCount>& data ) {
    if(data.size() == ParamInFeatScalarBufferCount && data.size() == ParamInFeatScalarBufferCount)
    {
      for(int i=0; i<ParamInFeatScalarBufferCount; i++)
      {
        if(enable[i]==true) data_[i].Accumulate(enable[i], data[i]);
      }
    }
    else
    {
      throw std::runtime_error("LINEAR_BUFFER Accumulate- size does not match");
    }
  }


  std::array<T,ParamInFeatScalarBufferCount> Read() const {
    int size = ParamInFeatScalarBufferCount;
    std::array<T, ParamInFeatScalarBufferCount> buffer_content = {};
    for(int i=0; i<ParamInFeatScalarBufferCount; i++ )
    {
      buffer_content[i] = data_[i].Read();
    }
    return buffer_content;
  }

};
#endif // #ifndef BUFFER_1D_H