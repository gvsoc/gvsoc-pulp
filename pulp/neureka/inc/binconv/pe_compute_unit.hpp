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
#ifndef PE_COMPUTE_H
#define PE_COMPUTE_H
#include "datatype.hpp"
template <typename HwpeType, typename T1, typename ST1, typename T2, int ParamColumnPerPECount, int ParamBinConvPerColumnCount>
/**
 * @brief Processing Engine Unit Class
 *
 * The `PEComputeUnit()`class takes care of Binary Convolution across the processing engine.
 * The processing engine consists of parametric `column_per_pe_count_` number of columns and each of these columns consists of parametric `binconv_per_column_count_` number of BinconvUnits
 *
 */
class PEComputeUnit {

private:
/**
 * @brief The PE consists of column_per_pe_count_ units of column_units_. Each ColumnUnit comprises of binconv_per_column_count_ number of BinconvUnits
 * 
 */
std::array<ColumnUnit<HwpeType,T1,ST1,T2, ParamBinConvPerColumnCount>, ParamColumnPerPECount> column_units_{};

HwpeType* accel_instance_;

public:
    /**
     * @brief Construct a new Processing Engine Unit object where the column_units_[i] is attached to a single ColumnUnit
     * 
     */
    PEComputeUnit(){};
    PEComputeUnit(HwpeType* accel,int ColumnPerPECount, int BinConvPerColumnCount)
    {
        accel_instance_ = accel;
    }

    void ComputePartialSum(Mode& mode, const std::array<std::array<bool, ParamBinConvPerColumnCount>, ParamColumnPerPECount>& enable,  const std::array<std::array<T1, ParamBinConvPerColumnCount>, ParamColumnPerPECount>& infeat, const std::array<std::array<T1, ParamBinConvPerColumnCount>, ParamColumnPerPECount>& weight, const std::array<T1, ParamColumnPerPECount>& shift, std::array<T2, ParamColumnPerPECount>&  sum_array, T2&  sum, const bool is_signed=false) {
        for (int i = 0; i < ParamColumnPerPECount; i++) {
            sum_array[i] = column_units_[i].ComputePartialSum(enable[i], infeat[i], weight[i], shift[i], is_signed);
            sum += sum_array[i];
        }
    }
};
#endif