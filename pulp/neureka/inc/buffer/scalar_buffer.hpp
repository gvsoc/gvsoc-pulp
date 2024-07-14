
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
#ifndef SCALAR_BUFFER_H
#define SCALAR_BUFFER_H
#include "datatype.hpp"
template <typename T>

/**
 * @brief This is a class handing single buffer (0 dimensional). 
 *  
 * It is a single memory of a certain datatype given by T. It supports type such as uint16_t, uint32_t, uint8_t etc.
 */
class ScalarBuffer {
private:
  /**
   * @brief Single element of the buffer. `T` is the data type of the buffer
   * 
   */
    T data_{};
public:
    /**
     * @brief Default Constructor. It initializes the private variable to 0.
     *
     */
    ScalarBuffer() {}

    /**
     * @brief Helper function to read the content of the buffer.
     *
     * @return Returns the value stored in the buffer. The const parameter ensures no changes to the read data. 
     */
    const T& Read() const {
        return data_;
    }

    /**
     * @brief Write writes value to the buffer when enable is true
     *
     * @param enable when enabled the value is written to the buffer  
     * @param value is written to the buffer when enable is set to true 
     */
    void Write( bool enable, const T& value) {
        data_ = (enable == true) ? value : data_;
    }

    /**
     * @brief Reset writes reset value to the buffer when enable is true. This can be done using Write also but adding a reset function adds more readadbility
     *
     * @param enable when enabled the value is written to the buffer  
     * @param value is written to the buffer when enable is set to true. 
     */

    void Reset( bool enable, const T& value = 0) {
        data_ = (enable == true) ? value : data_;
    }

    /**
     * @brief Accumulate, accumulates value on top of the value stored in the buffer.
     *
     * @param enable when enabled the value is accumulated on the buffer  
     * @param value is the value to be accumulated. 
     */

    void Accumulate(bool enable, const T& value)
    {
      if( enable == true )
        data_ += value;
    } 


};
#endif // 