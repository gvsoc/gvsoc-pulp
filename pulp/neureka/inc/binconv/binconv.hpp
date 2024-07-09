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
#ifndef BINCONV_H
#define BINCONV_H
#include <iostream>
#include <cstdint>
#include <vector>
#include <array>
// #include <xtensor/xarray.hpp>
// #include <xtensor/xmath.hpp>
// #include <xtensor/xrandom.hpp>
// #include <xtensor/xio.hpp>
#include "debug.hpp"
#include "datatype.hpp"
template <typename HwpeType, typename T1, typename ST1, typename T2>
/**
 * @brief Binary Convolution Unit Class
 *
 * The `BinconvUnit()`class takes care of Binary Convolution.
 * It takes an T type input value  and multiplies it with weight to produce the convolved value
 *
 */
class BinconvUnit {
public:
    /**
     * @brief Construct a new Binconv Unit object a default constructor
     * 
     * Assign the partial sum to 0 while invoking the constructor. 
     */
    BinconvUnit(){};
    BinconvUnit(HwpeType* accel){ accel_instance_ = accel;};
     /**
     * @brief Computes partial sum by multiplying inputfeature and weight and store it to `psum_binconv_`
     * 
     * @param infeat Single Input feature of type T1
     * @param weight Single weight element, It can be either 1 or 0. 
     * @param enable Mask when set to `true` partial sum is computed
     */
    T2 inline ComputePartialSum( const bool enable, const T1 infeat, const T1 weight, const bool is_signed=false) {
        // this->accel_instance_->trace.msg("<<<< ComputePartialSum start\n");
        
        if(is_signed)
        {
            psum_binconv_ = (enable == true) ? (T2)((ST1)infeat) * (T2)weight : (T2)0;
        }
        else
            psum_binconv_ = (enable == true) ? (T2)infeat * (T2)weight : (T2)0;
        // if(accel_instance_->trace_config.binconv)
        //     accel_instance_->trace.msg(" BinconvModule: enable=%d, infeat=%x, weight=%x, psum=%x\n", enable, infeat, weight, psum_binconv_);
        // this->accel_instance_->trace.msg("<<<< ComputePartialSum end\n");
            
        return psum_binconv_;
    }

private:
    /**
     * @brief Single element to store the psum value. It is of type `T2`
     * 
     */
    T2 psum_binconv_{};
    HwpeType* accel_instance_;
};
#endif