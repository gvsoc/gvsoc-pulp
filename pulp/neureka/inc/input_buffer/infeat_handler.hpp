
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
#ifndef INPUT_BUFFER_H
#define INPUT_BUFFER_H

#include "datatype.hpp"
#include "params.hpp"
#include "matrix_buffer.hpp"
template <typename HwpeType>

class InFeatBuffer{
    private:
    std::array<MatrixBuffer<InFeatType, NeurekaInFeatLinearBufferCount, NeurekaInFeatScalarBufferCount>, NeurekaInFeatBufferInstanceCount> infeat_buffer_instance_; 
    std::array<std::array<std::array<InFeatType, NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>,NeurekaTotalPECountXY> infeat_mapped_to_pe_;
    HwpeType* hwpe_instance_;
    public:
    InFeatBuffer(){}
    InFeatBuffer(HwpeType* hwpe_instance)
    {
      hwpe_instance_ = hwpe_instance;
    }

void WriteLinearBufferAtIndex(const int& index, const int& linear_buffer_index, const std::array<bool, NeurekaInFeatScalarBufferCount>& enable, const std::array<InFeatType, NeurekaInFeatScalarBufferCount>& data)
{
  infeat_buffer_instance_[index].WriteLinearBufferAtIndex(linear_buffer_index, enable, data);
}

void WriteScalarBufferAtIndex(const int& index, const int& linear_buffer_index, const int& scalar_buffer_index, const bool& enable, const InFeatType& data)
{
  infeat_buffer_instance_[index].WriteScalarBufferAtIndex(linear_buffer_index, scalar_buffer_index, enable, data);
}

InFeatType ReadScalarBufferFromIndex(const int& linear_buffer_index, const int& scalar_buffer_index, const int& index) const
{
  return infeat_buffer_instance_[index].ReadScalarBufferFromIndex(linear_buffer_index, scalar_buffer_index);
}

std::array<std::array<std::array<InFeatType, NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>,NeurekaTotalPECountXY>  ReadInFeatMappedToPE() const
{
  return infeat_mapped_to_pe_;
}

void MapInFeatToEnginesIn1x1(const int& index) {
  std::array<std::array<InFeatType, NeurekaInFeatScalarBufferCount>, NeurekaInFeatLinearBufferCount> buffer_data;//64x32
  std::array<std::array<std::array<InFeatType, NeurekaComputeRowCount>, NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> pe_data;//64x32
  infeat_buffer_instance_[index].Read(buffer_data);  
  for(int h=0; h<NeurekaPECountY; h++) {//PE loop across H
    for(int w=0; w<NeurekaPECountX; w++) {//PE loop across W
      int pe_index = h*NeurekaPECountX + w;
      int buffer_pe_index = h*NeurekaInFeatBufferSizeX + w;
      for(int col_index=0; col_index<NeurekaInFeatScalarBufferCount; col_index++){
        for(int row_index=0; row_index<NeurekaComputeRowCount-1; row_index++){
          int buffer_rem_index = col_index % 4; 
          int buffer_col_index = buffer_rem_index * 8 + row_index; 
          pe_data[pe_index][col_index][row_index] = buffer_data[buffer_pe_index][buffer_col_index];
        }
        pe_data[pe_index][col_index][NeurekaComputeRowCount-1] = 0;
      }
    }
  }

    for(int i=0; i<NeurekaTotalPECountXY; i++){
      for(int j=0; j<NeurekaInFeatScalarBufferCount; j++){
        for(int k=0; k<NeurekaComputeRowCount; k++){
          infeat_mapped_to_pe_[i][j][k] = pe_data[i][j][k];
        }
      }
    }
}

void print_input_buffer(int index){
  std::array<std::array<InFeatType, NeurekaInFeatScalarBufferCount>, NeurekaInFeatLinearBufferCount> buffer_data;//64x32

  infeat_buffer_instance_[index].Read(buffer_data);  
  for(int i=0; i<NeurekaInFeatLinearBufferCount; i++)
  {
    std::cout<<"------------------------- ROW "<<i<<" ---------------------------\n";
    for(int j=0; j<NeurekaInFeatScalarBufferCount; j++){
      std::cout<<(int) buffer_data[i][j]<< " ";
    }
  }
  std::cout<<"\n";
}
void MapInFeatToEnginesIn3x3(const int& index) {
  std::array<std::array<InFeatType, NeurekaInFeatScalarBufferCount>, NeurekaInFeatLinearBufferCount> buffer_data;//64x32
  std::array<std::array<std::array<InFeatType, NeurekaComputeRowCount>, NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> pe_data;//64x32

  // print_input_buffer(index);

  infeat_buffer_instance_[index].Read(buffer_data);  

  for(int h=0; h<NeurekaPECountY; h++) {//PE loop across H
    for(int w=0; w<NeurekaPECountX; w++) {//PE loop across W
      int pe_index = h*NeurekaPECountX+w; ;
      for(int fs_i=0; fs_i<NeurekaFilterSize; fs_i++) {
        for(int fs_j=0; fs_j<NeurekaFilterSize; fs_j++) {
          int row_index = fs_i * NeurekaFilterSize + fs_j;
          for(int col_index=0; col_index<NeurekaInFeatScalarBufferCount; col_index++){
            pe_data[pe_index][col_index][row_index] = buffer_data[(h+fs_i)*NeurekaInFeatBufferSizeX+w+fs_j][col_index];
          }
        }
      }
    }

    for(int i=0; i<NeurekaTotalPECountXY; i++){
      for(int j=0; j<NeurekaInFeatScalarBufferCount; j++){
        for(int k=0; k<NeurekaComputeRowCount; k++){
          infeat_mapped_to_pe_[i][j][k] = pe_data[i][j][k];
        }
      }
    }
  }

}


void MapInFeatToEngines(const int& index, const Mode& mode)
{
  if(mode==Pointwise)
    MapInFeatToEnginesIn1x1(index);
  else
    MapInFeatToEnginesIn3x3(index);
}



};
#endif //INPUT_BUFFER_H
