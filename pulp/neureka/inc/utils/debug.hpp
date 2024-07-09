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

#ifndef DEBUG_H
#define DEBUG_H
#include <iostream>
#include <cstdint>
#include <iostream>
#include <cstdint>
#include <vector>
#include <array>


template <typename DataType>
void printXtensorXarray(const DataType& xarr, const char* debug_msg) 
{

    auto shape = xarr.shape();

    std::cout<<std::hex<<"\n******************* Printing "<<debug_msg<<" with dimension [" ;
    for (int i = 0; i < shape.size(); ++i) {
        std::cout <<std::dec<< shape[i];
        if (i < shape.size() - 1) {
            std::cout<<std::dec<< ", ";
        }
    } 
    std::cout<< "]:" << std::endl;
    std::cout <<std::dec<< xarr << std::endl;
}

#endif // DEBUG_H