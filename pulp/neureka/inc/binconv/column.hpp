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
#ifndef COLUMN_H
#define COLUMN_H
#include "datatype.hpp"
template <typename HwpeType, typename T1, typename ST1, typename T2, int ParamBinConvPerColumnCount>
/**
 * @brief Column Unit Class
 *
 * The `ColumnUnit()`class takes care of Binary Convolution across column.
 * Each column consists of `BinConvPerColumnCount` units of `BinconvUnit`. In NEUREKA `BinConvPerColumnCount` is set to 9
 *
 */
class ColumnUnit {

private: 
     /**
     * @brief Instantiates `BinConvPerColumnCount` units of `BinconvUnit`
     * 
     */
    std::array<BinconvUnit<HwpeType, T1, ST1, T2>, ParamBinConvPerColumnCount> binconv_units_{};
    HwpeType* accel_instance_;
public:
    ColumnUnit(){};
    ColumnUnit(HwpeType* accel, int BinConvPerColumnCount)
    {
        accel_instance_ = accel;
    }

     T2 inline ComputePartialSum(const std::array<bool, ParamBinConvPerColumnCount>& enable, const std::array<T1, ParamBinConvPerColumnCount>& infeat, const std::array<T1, ParamBinConvPerColumnCount>& weight, const T1 shift = 0, const bool is_signed=false)
    {
        T2 sum = 0;

        for (int i = 0; i < ParamBinConvPerColumnCount; i++) {
            sum += binconv_units_[i].ComputePartialSum(enable[i], infeat[i], weight[i], is_signed);
        }

        return (sum << shift);
    }

};
#endif