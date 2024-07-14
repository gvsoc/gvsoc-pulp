
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
#ifndef PARAMS_H
#define PARAMS_H
#define NEUREKA_REG_WEIGHTS_PTR       0
#define NEUREKA_REG_INFEAT_PTR        1
#define NEUREKA_REG_OUTFEAT_PTR       2
#define NEUREKA_REG_SCALE_PTR         3
#define NEUREKA_REG_SCALE_SHIFT_PTR   4
#define NEUREKA_REG_SCALE_BIAS_PTR    5
#define NEUREKA_REG_STREAMIN_PTR      6
#define NEUREKA_REG_INFEAT_D0_STRIDE  7
#define NEUREKA_REG_INFEAT_D1_STRIDE  8
#define NEUREKA_REG_INFEAT_D2_STRIDE  9
#define NEUREKA_REG_OUTFEAT_D0_STRIDE 10
#define NEUREKA_REG_OUTFEAT_D1_STRIDE 11
#define NEUREKA_REG_OUTFEAT_D2_STRIDE 12
#define NEUREKA_REG_WEIGHTS_D0_STRIDE 13
#define NEUREKA_REG_WEIGHTS_D1_STRIDE 14
#define NEUREKA_REG_WEIGHTS_D2_STRIDE 15
#define NEUREKA_REG_SUBTILE_REM0      16
#define NEUREKA_REG_SUBTILE_REM1      17
#define NEUREKA_REG_SUBTILE_REM2      18
#define NEUREKA_REG_SUBTILE_NB0       19
#define NEUREKA_REG_SUBTILE_NB1       20
#define NEUREKA_REG_PADDING           21
#define NEUREKA_REG_WEIGHT_OFFSET     22
#define NEUREKA_REG_FILTER_MASK       23
#define NEUREKA_REG_CONFIG0           24



#define NEUREKA_NB_REG 25

#define NEUREKA_SPECIAL_TRACE_REG NEUREKA_NB_REG
#define NEUREKA_SPECIAL_FORMAT_TRACE_REG NEUREKA_NB_REG+1
#define DEFAULT_TRACE_LEVEL LEVEL_DEBUG


#define NeurekaFilterSize 3
#define NeurekaComputeRowCount NeurekaFilterSize*NeurekaFilterSize // 9 rows in the PE array
#define NeurekaWeightBitCount 8
#define NeurekaAccumulatorPerPECount 32// TP_OUT
#define NeurekaPECountX 6
#define NeurekaPECountY 6
#define NeurekaInFeatBufferSizeX ((NeurekaPECountX)+2)
#define NeurekaInFeatBufferSizeY ((NeurekaPECountY)+2)
#define NeurekaInFeatScalarBufferCount 32// TP_IN channels
#define NeurekaChannelwise1x1 (NeurekaInFeatScalarBufferCount/NeurekaWeightBitCount)
#define NeurekaRepeated1x1 (NeurekaInFeatScalarBufferCount/NeurekaChannelwise1x1)
#define NeurekaInFeatLinearBufferCount (NeurekaInFeatBufferSizeX*NeurekaInFeatBufferSizeY)//Total feature size 8x8
#define NeurekaTotalPECountXY ((NeurekaPECountX)*(NeurekaPECountY))// Number of PEs 
#define NeurekaInFeatBufferInstanceCount 2 // For double buffering
#define NeurekaColumnPerPECount   NeurekaInFeatScalarBufferCount// Matches with TP_IN
#define NeurekaBinConvPerColumnCount   NeurekaComputeRowCount
#define NeurekaRegisterContextCount 2 
#define DenseWoffsetOverhead 6
#define DepthwiseWoffsetOverhead (DenseWoffsetOverhead+NeurekaAccumulatorPerPECount)
#define NeurekaNormMultiplierCount 1 
#define NeurekaNormAdderCount 8




#endif 