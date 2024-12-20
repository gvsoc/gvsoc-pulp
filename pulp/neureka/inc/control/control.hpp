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
#ifndef CONTROL_H
#define CONTROL_H
#include "debug.hpp"
#include "datatype.hpp"
#include "params.hpp"
template <typename HwpeType>
class Control 
{
  private:
    std::array<bool,NeurekaTotalPECountXY> pe_enable_;
    std::array<std::array<bool, NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> col_enable_;
    std::array<std::array<std::array<bool, NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> binconv_enable_;

    HwpeType* hwpe_instance_;
    HwParams hw_param_;
    RegConfig ctrl_config_;


  public:

    TilingParams<int> current_tile_size, total_size;
    TilingStatus tiles, prev_tiles;
    TilingStatus prefetch_tiles, prefetch_prev_tiles;
    LoadStoreStatus load_store_status, prefetch_load_store_status;


    Control() {}
    Control(HwpeType* hwpe_instance){
      hwpe_instance_ = hwpe_instance;
      tiles = {};
    }


  void ResetIndexes(){
    tiles = {};
    prev_tiles={};
    prefetch_tiles = {};
    prefetch_prev_tiles={};
    load_store_status={};
    prefetch_load_store_status={};
    current_tile_size={};
    total_size={};
  }
  void ComputeDimensions() {
    if(ctrl_config_.config0.infeat_prefetch==false)
      prefetch_tiles = tiles; 
    if(ctrl_config_.config0.filter_mode != Pointwise) {
      load_store_status.infeat.count.win = prefetch_tiles.index.wout < prefetch_tiles.count.wout-1 ? NeurekaInFeatBufferSizeX : (ctrl_config_.win_tile_rem ? ctrl_config_.win_tile_rem : NeurekaInFeatBufferSizeX);
      load_store_status.infeat.count.hin = prefetch_tiles.index.hout < prefetch_tiles.count.hout-1 ? NeurekaInFeatBufferSizeY : (ctrl_config_.hin_tile_rem ? ctrl_config_.hin_tile_rem : NeurekaInFeatBufferSizeY);  
    } else {
      load_store_status.infeat.count.win = prefetch_tiles.index.wout < prefetch_tiles.count.wout-1 ? NeurekaPECountX : (ctrl_config_.win_tile_rem ? ctrl_config_.win_tile_rem : NeurekaPECountX);
      load_store_status.infeat.count.hin = prefetch_tiles.index.hout < prefetch_tiles.count.hout-1 ? NeurekaPECountY : (ctrl_config_.hin_tile_rem ? ctrl_config_.hin_tile_rem : NeurekaPECountY);
    }
    load_store_status.outfeat.count.wout = tiles.index.wout < tiles.count.wout-1 ? NeurekaPECountX : (ctrl_config_.wout_tile_rem ? ctrl_config_.wout_tile_rem : NeurekaPECountX);
    load_store_status.outfeat.count.hout = tiles.index.hout < tiles.count.hout-1 ? NeurekaPECountY : (ctrl_config_.hout_tile_rem ? ctrl_config_.hout_tile_rem : NeurekaPECountY);
    load_store_status.streamin.count.wout = load_store_status.outfeat.count.wout;
    load_store_status.streamin.count.hout = load_store_status.outfeat.count.hout;
    current_tile_size.kin  = tiles.index.kin  < tiles.count.kin-1  ? NeurekaInFeatScalarBufferCount : (ctrl_config_.kin_tile_rem ? ctrl_config_.kin_tile_rem : NeurekaInFeatScalarBufferCount) ;
    current_tile_size.kout = tiles.index.kout  < tiles.count.kout-1  ? NeurekaAccumulatorPerPECount : (ctrl_config_.kout_tile_rem? ctrl_config_.kout_tile_rem : NeurekaAccumulatorPerPECount);
    
    int current_tile_sizekout_rem = (current_tile_size.kout & 0x7) ? (current_tile_size.kout & 0x7)  : 8;
    int current_tile_sizekout_quo = (current_tile_size.kout & 0x7) ? 1 + (current_tile_size.kout>>3) : (current_tile_size.kout>>3);
    load_store_status.outfeat.count.word = ctrl_config_.config0.quantization_bit_count == 8 ? 1 : current_tile_sizekout_quo;
    load_store_status.streamin.count.word = ctrl_config_.config0.streamin_bit_count == 8 ? 1 : current_tile_sizekout_quo;

    total_size.kin  = (tiles.count.kin-1)*NeurekaInFeatScalarBufferCount + ctrl_config_.kin_tile_rem;
    total_size.kout = (tiles.count.kout-1)*NeurekaAccumulatorPerPECount + ctrl_config_.kout_tile_rem;
    total_size.hout = (tiles.count.hout-1)*NeurekaPECountY  + ctrl_config_.hout_tile_rem;
    total_size.wout = (tiles.count.wout-1)*NeurekaPECountX  + ctrl_config_.wout_tile_rem;
    total_size.hin  = (tiles.count.hout-1)*NeurekaPECountY  + ctrl_config_.hin_tile_rem;
    total_size.win  = (tiles.count.wout-1)*NeurekaPECountX  + ctrl_config_.win_tile_rem;


    
    // std::cout<<"load_store_status.outfeat.count.word="<<load_store_status.outfeat.count.word<<"\n";
    // std::cout<<"load_store_status.infeat.count.hin="<<load_store_status.infeat.count.hin<<" load_store_status.infeat.count.win="<<load_store_status.infeat.count.win<<"\n";
    // std::cout<<"tiles.count.win="<<tiles.count.win<<" tiles.count.hin="<<tiles.count.hin<<"\n";
    // hwpe_instance_->trace.msg(" control >> 2 ComputeDimensions INDEXES (kin : %d, kout : %d, hout : %d, wout : %d)\n",tiles.index.kin, tiles.index.kout, tiles.index.hout, tiles.index.wout);
    // hwpe_instance_->trace.msg(" control >> total size (kin : %d, kout : %d, hout : %d, wout : %d)\n",total_size.kin, total_size.kout, total_size.hout, total_size.wout);
  }

std::array<bool, NeurekaTotalPECountXY>  ComputePEEnable(){
  ComputeDimensions();
  std::fill(pe_enable_.begin(), pe_enable_.end(), 0);
  for(int y=0; y<NeurekaPECountY; y++) {
    for(int x=0; x<NeurekaPECountX; x++) {
      if(y<load_store_status.outfeat.count.hout && x<load_store_status.outfeat.count.wout)
        pe_enable_[y*NeurekaPECountX+x] = true;
    }
  }
  // printXtensorXarray(pe_enable_, "PE Enable");
  return pe_enable_;
}

std::array<std::array<bool, NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY>  ComputeColumnEnable(bool woffs) {
  auto temp = ComputePEEnable();
  col_enable_ = {};
  for(int pe=0; pe<NeurekaTotalPECountXY; pe++) {
    if(ctrl_config_.config0.filter_mode == Pointwise){
      for(int wgt_bit=0; wgt_bit<ctrl_config_.config0.weight_bit_count; wgt_bit++) {
        for(int i=0; i<NeurekaChannelwise1x1; i++) {
          int index = wgt_bit*NeurekaChannelwise1x1 + i;
          if(woffs && wgt_bit>0)
            col_enable_[pe][index] = false; // only 4 columns used in pointwise mode for weight offset calculation
          else
            col_enable_[pe][index] = true & pe_enable_[pe];
        }
      }
    } else {
    for(int col=0; col<NeurekaInFeatScalarBufferCount; col++)
      if(ctrl_config_.config0.filter_mode != Depthwise){
        if(col<current_tile_size.kin)
          col_enable_[pe][col] = (true & pe_enable_[pe]);
        else
          col_enable_[pe][col] = false;
      } else {
        if(col<current_tile_size.kout)
          col_enable_[pe][col] = (true & pe_enable_[pe]);
      }
    }
  }
  return col_enable_;
}

std::array<std::array<std::array<bool, NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> ComputeBinconvEnable(bool woffs){
  auto temp = ComputeColumnEnable(woffs);
  std::array<std::array<std::array<bool, NeurekaComputeRowCount>,NeurekaInFeatScalarBufferCount>, NeurekaTotalPECountXY> binconv_enable_ = {0};
  for(int pe=0; pe<NeurekaTotalPECountXY; pe++){
    if(ctrl_config_.config0.filter_mode == Pointwise) {
      for(int wgt_bit=0; wgt_bit<ctrl_config_.config0.weight_bit_count; wgt_bit++) { // repeated 8 times max 
        for(int i=0; i<NeurekaChannelwise1x1; i++) { // 4 columns
          int col_index = wgt_bit*NeurekaChannelwise1x1 + i;  
          for(int row=0; row<NeurekaComputeRowCount-1; row++) {
            int row_index =  i*NeurekaRepeated1x1 + row; 
            if(row_index<current_tile_size.kin)
              binconv_enable_[pe][col_index][row] = true & col_enable_[pe][col_index] & (~ctrl_config_.filter_mask_bit[row]);
          }
        }
      }
    } else {
    for(int col=0; col<NeurekaInFeatScalarBufferCount; col++)
      for(int row=0; row<NeurekaComputeRowCount; row++)
      {
          binconv_enable_[pe][col][row] = true & col_enable_[pe][col] & (~ctrl_config_.filter_mask_bit[row]);
      }
    }
  }

  return binconv_enable_;
}


void UpdateTileIndex() {
    // std::cout<<"UpdateTileIndex RECAHED HERE 0 \n";
    prev_tiles = tiles;
    // std::cout<<"UpdateTileIndex kout_index  = "<<tiles.index.kout<<"\n";
    // std::cout<<"UpdateTileIndex kout_count  = "<<tiles.count.kout<<"\n";
    if (tiles.index.kout >= tiles.count.kout) {
        return; // Nothing to do, all iterations already completed
    }
    // std::cout<<"UpdateTileIndex RECAHED HERE 1 \n";

    // Your code here
    int offset_h = 0; 
    int offset_w = 0;
    tiles.index.kin++;
    if (tiles.index.kin >= tiles.count.kin) {
        tiles.index.kin = 0;
        tiles.index.wout++;
        if(ctrl_config_.config0.strided2x2 && total_size.wout % NeurekaPECountX == 1 && tiles.count.wout>1 && tiles.count.wout%2==0 && NeurekaPECountX%2==1)
          offset_w = 1;
        if(ctrl_config_.config0.strided2x2 && total_size.hout % NeurekaPECountY == 1 && tiles.count.hout>1 && tiles.count.hout%2==0 && NeurekaPECountY%2==1)
          offset_h = 1;
        if (tiles.index.wout >= tiles.count.wout-offset_w) {
            tiles.index.wout = 0;
            tiles.index.hout++;
            if (tiles.index.hout >= tiles.count.hout-offset_h) {
                tiles.index.hout = 0;
                tiles.index.kout++;
            }
        }
    }
  tiles.index.win = tiles.index.wout;
  tiles.index.hin = tiles.index.hout;

  if(ctrl_config_.config0.infeat_prefetch==false)
    prefetch_tiles = tiles; 
  // hwpe_instance_->trace.msg(" control >> UpdateTileIndex End INDEXES (kin : %d, kout : %d, hout : %d, wout : %d)\n",tiles.index.kin, tiles.index.kout, tiles.index.hout, tiles.index.wout);
  // hwpe_instance_->trace.msg(" control >> UpdateTileIndex End COUNTS (kin : %d, kout : %d, hout : %d, wout : %d)\n", tiles.count.kin, tiles.count.kout, tiles.count.hout, tiles.count.wout);
  // hwpe_instance_->trace.msg(" control >> UpdateTileIndex End done : %d, DONE (kin : %d, kout : %d, hout : %d, wout : %d)\n", tiles.finish, tiles.done.kin, tiles.done.kout, tiles.done.hout, tiles.done.wout);

}


void PrefetchUpdateTileIndex() {
    // std::cout<<"PrefetchUpdateTileIndex RECAHED HERE 0 \n";
    prefetch_prev_tiles = prefetch_tiles;
    // std::cout<<"PrefetchUpdateTileIndex kout_index  = "<<prefetch_tiles.index.kout<<"\n";
    // std::cout<<"PrefetchUpdateTileIndex kout_count  = "<<prefetch_tiles.count.kout<<"\n";
    if (prefetch_tiles.index.kout >= prefetch_tiles.count.kout) {
        return; // Nothing to do, all iterations already completed
    }

    // Your code here

    // std::cout<<"PrefetchUpdateTileIndex RECAHED HERE 1 \n";
    int offset_h = 0;
    int offset_w = 0;
    prefetch_tiles.index.kin++;
    if (prefetch_tiles.index.kin >= prefetch_tiles.count.kin) {
        prefetch_tiles.index.kin = 0;
        prefetch_tiles.index.wout++;
        if(ctrl_config_.config0.strided2x2 && total_size.wout % NeurekaPECountX == 1 && prefetch_tiles.count.wout>1 && prefetch_tiles.count.wout%2==0  && NeurekaPECountX%2==1)
          offset_w = 1;
        if(ctrl_config_.config0.strided2x2 && total_size.hout % NeurekaPECountY == 1 && prefetch_tiles.count.hout>1 && prefetch_tiles.count.hout%2==0 && NeurekaPECountY%2==1)
          offset_h = 1;
        if (prefetch_tiles.index.wout >= prefetch_tiles.count.wout-offset_w) {
            prefetch_tiles.index.wout = 0;
            prefetch_tiles.index.hout++;
            if (prefetch_tiles.index.hout >= prefetch_tiles.count.hout-offset_h) {
                prefetch_tiles.index.hout = 0;
                prefetch_tiles.index.kout++;
            }
        }
    }
  prefetch_tiles.index.win = prefetch_tiles.index.wout;
  prefetch_tiles.index.hin = prefetch_tiles.index.hout;
  // hwpe_instance_->trace.msg(" control >> UpdateTileIndex End INDEXES (kin : %d, kout : %d, hout : %d, wout : %d)\n",tiles.index.kin, tiles.index.kout, tiles.index.hout, tiles.index.wout);
  // hwpe_instance_->trace.msg(" control >> UpdateTileIndex End COUNTS (kin : %d, kout : %d, hout : %d, wout : %d)\n", tiles.count.kin, tiles.count.kout, tiles.count.hout, tiles.count.wout);
  // hwpe_instance_->trace.msg(" control >> UpdateTileIndex End done : %d, DONE (kin : %d, kout : %d, hout : %d, wout : %d)\n", tiles.finish, tiles.done.kin, tiles.done.kout, tiles.done.hout, tiles.done.wout);

}

void CheckTileStatus(){
  tiles.done.kin  = tiles.index.kin == tiles.count.kin-1 ? true : false;
  tiles.done.kout = tiles.index.kout == tiles.count.kout-1 ? true : false;
  tiles.done.hout = tiles.index.hout == tiles.count.hout-1 ? true : false;
  tiles.done.wout = tiles.index.wout == tiles.count.wout-1 ? true : false;
  if(ctrl_config_.config0.strided2x2 && total_size.hout % NeurekaPECountY == 1 && tiles.count.hout>1 && tiles.count.hout%2==0 && NeurekaPECountY%2==1)
    tiles.done.hout = tiles.index.hout == tiles.count.hout-2 ? true : false;
  if(ctrl_config_.config0.strided2x2 && total_size.wout % NeurekaPECountX == 1 && tiles.count.wout>1 && tiles.count.wout%2==0 && NeurekaPECountX%2==1)
    tiles.done.wout = tiles.index.wout == tiles.count.wout-2 ? true : false;

  // std::cout << "tiles.count.hout = "<<tiles.count.hout<<"\n";
  // std::cout << "tiles.count.wout = "<<tiles.count.wout<<"\n";
  tiles.finish    = tiles.done.wout && tiles.done.hout && tiles.done.kout && tiles.done.kin;
  // hwpe_instance_->trace.msg(" control >> CheckTileStatus End INDEXES (kin : %d, kout : %d, hout : %d, wout : %d)\n",tiles.index.kin, tiles.index.kout, tiles.index.hout, tiles.index.wout);
  // hwpe_instance_->trace.msg(" control >> CheckTileStatus End COUNTS (kin : %d, kout : %d, hout : %d, wout : %d)\n", tiles.count.kin, tiles.count.kout, tiles.count.hout, tiles.count.wout);
  // hwpe_instance_->trace.msg(" control >> CheckTileStatus End done : %d, DONE (kin : %d, kout : %d, hout : %d, wout : %d)\n", tiles.finish, tiles.done.kin, tiles.done.kout, tiles.done.hout, tiles.done.wout);
}

void PrefetchCheckTileStatus(){
  prefetch_tiles.done.kin  = prefetch_tiles.index.kin == prefetch_tiles.count.kin-1 ? true : false;
  prefetch_tiles.done.kout = prefetch_tiles.index.kout == prefetch_tiles.count.kout-1 ? true : false;
  prefetch_tiles.done.hout = prefetch_tiles.index.hout == prefetch_tiles.count.hout-1 ? true : false;
  prefetch_tiles.done.wout = prefetch_tiles.index.wout == prefetch_tiles.count.wout-1 ? true : false;
   if(ctrl_config_.config0.strided2x2 && total_size.hout % NeurekaPECountY == 1 && prefetch_tiles.count.hout>1 && prefetch_tiles.count.hout%2==0 && NeurekaPECountY%2==1)
    prefetch_tiles.done.hout = prefetch_tiles.index.hout == prefetch_tiles.count.hout-2 ? true : false;
  if(ctrl_config_.config0.strided2x2 && total_size.wout % NeurekaPECountX == 1 && prefetch_tiles.count.wout>1 && prefetch_tiles.count.wout%2==0 && NeurekaPECountX%2==1)
    prefetch_tiles.done.wout = prefetch_tiles.index.wout == prefetch_tiles.count.wout-2 ? true : false;
  prefetch_tiles.finish    = prefetch_tiles.done.wout && prefetch_tiles.done.hout && prefetch_tiles.done.kout && prefetch_tiles.done.kin;
  // hwpe_instance_->trace.msg(" control >> PrefetchCheckTileStatus End INDEXES (kin : %d, kout : %d, hout : %d, wout : %d)\n",tiles.index.kin, tiles.index.kout, tiles.index.hout, tiles.index.wout);
  // hwpe_instance_->trace.msg(" control >> PrefetchCheckTileStatus End COUNTS (kin : %d, kout : %d, hout : %d, wout : %d)\n", tiles.count.kin, tiles.count.kout, tiles.count.hout, tiles.count.wout);
  // hwpe_instance_->trace.msg(" control >> PrefetchCheckTileStatus End done : %d, DONE (kin : %d, kout : %d, hout : %d, wout : %d)\n", tiles.finish, tiles.done.kin, tiles.done.kout, tiles.done.hout, tiles.done.wout);
}


void SetConfig(RegConfig config) {
  ctrl_config_ = config;
  // tiles.count.wout = ctrl_config_.wout_tile_count - 1;
  // tiles.count.hout = ctrl_config_.hout_tile_count - 1;
  // tiles.count.kin  = ctrl_config_.kin_tile_count - 1;
  // tiles.count.kout = ctrl_config_.kout_tile_count - 1;
  // tiles.count.wout = ctrl_config_.wout_tile_count;
  // tiles.count.hout = ctrl_config_.hout_tile_count;
  // tiles.count.kin  = ctrl_config_.config0.filter_mode == Depthwise ?1 : ctrl_config_.kin_tile_count;
  // tiles.count.kout = ctrl_config_.kout_tile_count;
  tiles.count.wout = ctrl_config_.wout_tile_count;
  tiles.count.hout = ctrl_config_.hout_tile_count;
  tiles.count.kin  = (ctrl_config_.config0.filter_mode == Depthwise ? 1 : ctrl_config_.kin_tile_count);
  tiles.count.kout = ctrl_config_.kout_tile_count;
  tiles.done.hout  = false;
  tiles.done.wout  = false;
  tiles.done.kout  = false;
  tiles.done.kin   = false;
  tiles.done.hin   = false;
  tiles.done.win   = false;
  tiles.finish     = false;
  tiles.count.hin  = tiles.count.hout;
  tiles.count.win  = tiles.count.wout;

  prefetch_tiles = tiles;

  hwpe_instance_->trace.msg(" control >> SetConfig INDEXES (kin : %d, kout : %d, hout : %d, wout : %d)\n",tiles.index.kin, tiles.index.kout, tiles.index.hout, tiles.index.wout);
  hwpe_instance_->trace.msg(" control >> SetConfig done : %d , COUNTS (kin : %d, kout : %d, hout : %d, wout : %d)\n",tiles.finish, tiles.count.kin, tiles.count.kout, tiles.count.hout, tiles.count.wout); 
  // PrintReg();
}

void ResetInFeatLoadIteration() {
  load_store_status.infeat.index.win = 0;
  load_store_status.infeat.index.hin = 0;
}

bool InFeatLoadLastIteration() {
  const auto& h_count = load_store_status.infeat.count.hin;
  const auto& w_count = load_store_status.infeat.count.win;
  const auto& h_index = load_store_status.infeat.index.hin;
  const auto& w_index = load_store_status.infeat.index.win;

  return w_index == w_count - 1 && h_index == h_count - 1;
}

void InFeatLoadUpdate() {
  const auto& h_count = load_store_status.infeat.count.hin;
  const auto& w_count = load_store_status.infeat.count.win;
  auto& h_index = load_store_status.infeat.index.hin;
  auto& w_index = load_store_status.infeat.index.win;

  ++w_index;
  if (w_index >= w_count) {
    w_index = 0;
    ++h_index;
    if (h_index >= h_count) {
      h_index = 0;
    }
  }
}

int GetWeightLoadWeightIndex(){ 
    return load_store_status.weight.index.wgt;// unused for depthwise mode
}

int GetWeightLoadKoutIndex(){ 
    return load_store_status.weight.index.kout;// unused for depthwise mode
}

void ResetWeightLoadIteration(){
  load_store_status.weight.index.kout=0;
  load_store_status.weight.index.wgt=0;
}

void WeightLoadIteration(){
  int qw_count = ctrl_config_.config0.weight_bit_count-1;
  int qw_index = load_store_status.weight.index.wgt;
  int kout_index = load_store_status.weight.index.kout;
  int kout_count = current_tile_size.kout-1;
  Mode mode = ctrl_config_.config0.filter_mode;

  load_store_status.weight.done = false;
  switch(mode){
    case Depthwise :
      load_store_status.weight.index.kout = 0;
      load_store_status.weight.index.wgt  = qw_index < qw_count ? load_store_status.weight.index.wgt+1 : load_store_status.weight.index.wgt;
      load_store_status.weight.done       = qw_index < qw_count ? false : true;
      break;
    case Pointwise : 
      load_store_status.weight.index.wgt  = 0;
      load_store_status.weight.index.kout = kout_index < kout_count ? load_store_status.weight.index.kout + 1 : load_store_status.weight.index.kout;
      load_store_status.weight.done       = kout_index < kout_count ? false : true;
      break;
    case Dense3x3 :
      load_store_status.weight.index.wgt  = qw_index == qw_count ? 0 : 1 + load_store_status.weight.index.wgt;
      load_store_status.weight.index.kout = (kout_index < kout_count) && (qw_index == qw_count) ? 1 + load_store_status.weight.index.kout : (kout_index == kout_count) && (qw_index == qw_count) ? 0 : load_store_status.weight.index.kout;
      load_store_status.weight.done       = (kout_index == kout_count) && (qw_index == qw_count) ? true : false;
      break;
    default:
      hwpe_instance_->trace.msg("Unsupported Filter mode");
  }
  // hwpe_instance_->trace.msg(" control >>  Weight Indexes ( wgt : %d, kout : %d ), Counts ( wgt : %d, kout : %d ) done : %d\n", load_store_status.weight.index.wgt, load_store_status.weight.index.kout, load_store_status.weight.count.wgt, load_store_status.weight.count.kout, load_store_status.weight.done);
}

void ResetOutFeatStoreIteration(){
  load_store_status.outfeat.done = false; 
  load_store_status.outfeat.index.word = 0;// Use for 8 and 32 bit quantizations
  load_store_status.outfeat.index.wout = 0;
  load_store_status.outfeat.index.hout = 0;
  if(ctrl_config_.config0.strided2x2){
    if(!(NeurekaPECountX%2==0 && NeurekaPECountY%2==0)){
      load_store_status.outfeat.index.wout = prev_tiles.index.wout % 2;
      load_store_status.outfeat.index.hout = prev_tiles.index.hout % 2;
    }

  }  
}

// generates a signal on what to store and what not.
int OutFeatStoreWidth(){
  int current_tile_sizekout_rem = (current_tile_size.kout & 0x7) ? (current_tile_size.kout & 0x7)  : 8;
  int current_tile_sizekout_quo = (current_tile_size.kout & 0x7) ? 1 + (current_tile_size.kout>>3) : (current_tile_size.kout>>3);
  if(ctrl_config_.config0.quantization_bit_count==32)
    if(load_store_status.outfeat.index.word < load_store_status.outfeat.count.word-1) return L1BandwidthInBytes;
    else return 4*current_tile_sizekout_rem;
  else return current_tile_size.kout;
}

void OutFeatStoreIteration(){
  int word_index = load_store_status.outfeat.index.word;
  int wout_index = load_store_status.outfeat.index.wout;
  int hout_index = load_store_status.outfeat.index.hout;
  int word_count = load_store_status.outfeat.count.word - 1;
  int hout_count = load_store_status.outfeat.count.hout - 1;
  int wout_count = load_store_status.outfeat.count.wout - 1;

  int hout_tile_odd = prev_tiles.index.hout % 2;
  int wout_tile_odd = prev_tiles.index.wout % 2;

  
  int offs = ctrl_config_.config0.strided2x2 ? 2 : 1;
  load_store_status.outfeat.index.word = (word_index  < word_count) ? 1 + load_store_status.outfeat.index.word : 0;

  // load_store_status.outfeat.index.hout = (word_index == word_count) && (wout_index+offs >= wout_count+1) && (hout_index+offs < hout_count+1) ? offs + load_store_status.outfeat.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index+offs >= hout_count+1) ? 0 : load_store_status.outfeat.index.hout;
  // load_store_status.outfeat.done       = (word_index == word_count) && ((wout_index+offs) >= (wout_count+1)) && ((hout_index+offs) >= (hout_count+1)) ? true : false;
  

  if(offs == 1){
    // load_store_status.outfeat.index.wout = (word_index == word_count) && (wout_index+offs < wout_count+1) ? offs + load_store_status.outfeat.index.wout : (word_index == word_count) && (wout_index+offs >= wout_count+1) ? 0 : load_store_status.outfeat.index.wout;
    // load_store_status.outfeat.index.hout = (word_index == word_count) && (wout_index+offs >= wout_count+1) && (hout_index+offs < hout_count+1) ? offs + load_store_status.outfeat.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index+offs >= hout_count+1) ? 0 : load_store_status.outfeat.index.hout;
    // load_store_status.outfeat.done       = (word_index == word_count) && ((wout_index+offs) >= (wout_count+1)) && ((hout_index+offs) >= (hout_count+1)) ? true : false;
      load_store_status.outfeat.index.wout = (word_index == word_count) && (wout_index+offs < wout_count+1) ? offs + load_store_status.outfeat.index.wout : (word_index == word_count) && (wout_index+offs >= wout_count+1) ? 0 : load_store_status.outfeat.index.wout;
  load_store_status.outfeat.index.hout = (word_index == word_count) && (wout_index+offs >= wout_count+1) && (hout_index+offs < hout_count+1) ? offs + load_store_status.outfeat.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index+offs >= hout_count+1) ? 0 : load_store_status.outfeat.index.hout;
  load_store_status.outfeat.done       = (word_index == word_count) && ((wout_index+offs) >= (wout_count+1)) && ((hout_index+offs) >= (hout_count+1)) ? true : false;
  } 
  else
  {
    if(NeurekaPECountX%2==0 && NeurekaPECountY%2==0){
      load_store_status.outfeat.index.wout = (word_index == word_count) && (wout_index+offs < wout_count+1) ? offs + load_store_status.outfeat.index.wout : (word_index == word_count) && (wout_index+offs >= wout_count+1) ? 0 : load_store_status.outfeat.index.wout;
      load_store_status.outfeat.index.hout = (word_index == word_count) && (wout_index+offs >= wout_count+1) && (hout_index+offs < hout_count+1) ? offs + load_store_status.outfeat.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index+offs >= hout_count+1) ? 0 : load_store_status.outfeat.index.hout;
      load_store_status.outfeat.done       = (word_index == word_count) && ((wout_index+offs) >= (wout_count+1)) && ((hout_index+offs) >= (hout_count+1)) ? true : false;
    } 
    else
    {
      if(hout_tile_odd && wout_tile_odd){
        load_store_status.outfeat.index.wout = (word_index == word_count) && (wout_index+offs < wout_count+1) ? offs + load_store_status.outfeat.index.wout : (word_index == word_count) && (wout_index+offs >= wout_count+1) ? 1 : load_store_status.outfeat.index.wout;
        load_store_status.outfeat.index.hout = (word_index == word_count) && (wout_index+offs >= wout_count+1) && (hout_index+offs < hout_count+1) ? offs + load_store_status.outfeat.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index+offs >= hout_count+1) ? 1 : load_store_status.outfeat.index.hout;
        load_store_status.outfeat.done       = (word_index == word_count) && ((wout_index+offs) >= (wout_count+1)) && ((hout_index+offs) >= (hout_count+1)) ? true : false;
      }else if(hout_tile_odd && (!wout_tile_odd)){
        load_store_status.outfeat.index.wout = (word_index == word_count) && (wout_index+offs < wout_count+1) ? offs + load_store_status.outfeat.index.wout : (word_index == word_count) && (wout_index+offs >= wout_count+1) ? 0 : load_store_status.outfeat.index.wout;
        load_store_status.outfeat.index.hout = (word_index == word_count) && (wout_index+offs >= wout_count+1) && (hout_index+offs < hout_count+1) ? offs + load_store_status.outfeat.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index+offs >= hout_count+1) ? 1 : load_store_status.outfeat.index.hout;
        load_store_status.outfeat.done       = (word_index == word_count) && ((wout_index+offs) >= (wout_count+1)) && ((hout_index+offs) >= (hout_count+1)) ? true : false;
      }else if((!hout_tile_odd) && wout_tile_odd){
        load_store_status.outfeat.index.wout = (word_index == word_count) && (wout_index+offs < wout_count+1) ? offs + load_store_status.outfeat.index.wout : (word_index == word_count) && (wout_index+offs >= wout_count+1) ? 1 : load_store_status.outfeat.index.wout;
        load_store_status.outfeat.index.hout = (word_index == word_count) && (wout_index+offs >= wout_count+1) && (hout_index+offs < hout_count+1) ? offs + load_store_status.outfeat.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index+offs >= hout_count+1) ? 0 : load_store_status.outfeat.index.hout;
        load_store_status.outfeat.done       = (word_index == word_count) && ((wout_index+offs) >= (wout_count+1)) && ((hout_index+offs) >= (hout_count+1)) ? true : false;
      } 
      else {
        load_store_status.outfeat.index.wout = (word_index == word_count) && (wout_index+offs < wout_count+1) ? offs + load_store_status.outfeat.index.wout : (word_index == word_count) && (wout_index+offs >= wout_count+1) ? 0 : load_store_status.outfeat.index.wout;
        load_store_status.outfeat.index.hout = (word_index == word_count) && (wout_index+offs >= wout_count+1) && (hout_index+offs < hout_count+1) ? offs + load_store_status.outfeat.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index+offs >= hout_count+1) ? 0 : load_store_status.outfeat.index.hout;
        load_store_status.outfeat.done       = (word_index == word_count) && ((wout_index+offs) >= (wout_count+1)) && ((hout_index+offs) >= (hout_count+1)) ? true : false;
      }
    }
  }
  // Calculate PE index considering both even and odd configurations
  // int index = load_store_status.outfeat.index.hout * NeurekaPECountX + load_store_status.outfeat.index.wout;


  // hwpe_instance_->trace.msg(" control >>  Store Indexes ( word : %d, wout : %d, hout : %d ), Counts ( word : %d, wout : %d, hout : %d ) done : %d\n", load_store_status.outfeat.index.word, load_store_status.outfeat.index.wout, load_store_status.outfeat.index.hout, load_store_status.outfeat.count.word, load_store_status.outfeat.count.wout, load_store_status.outfeat.count.hout, load_store_status.outfeat.done);
}

int GetOutFeatStoreLinearBufferIndex(){ 
  int index = load_store_status.outfeat.index.hout*NeurekaPECountX + load_store_status.outfeat.index.wout;
  // std::cout<<"load_store_status.outfeat.index.hout = "<<load_store_status.outfeat.index.hout<<"\n";
  // std::cout<<"load_store_status.outfeat.index.wout = "<<load_store_status.outfeat.index.wout<<"\n";
  // hwpe_instance_->trace.msg(" control >>  store linear buffer index = %d\n", index);
  return index;// unused for depthwise mode
}

int GetOutFeatStoreWordIndex(){ 
  int index = (load_store_status.outfeat.index.word<<3);
  // hwpe_instance_->trace.msg(" control >>  store word index = %d\n", index);
  return index;// unused for depthwise mode
}

int GetOutFeatStoreWordSize(){ 
  int index = load_store_status.outfeat.count.word;
  // hwpe_instance_->trace.msg(" control >>  store word size = %d\n", index);
  return index;// unused for depthwise mode
}


int GetStreaminLinearBufferIndex(){ 
  int index = load_store_status.streamin.index.hout*NeurekaPECountX + load_store_status.streamin.index.wout;
  // hwpe_instance_->trace.msg(" control >>  store linear buffer index = %d\n", index);
  return index;// unused for depthwise mode
}

int GetStreaminWordIndex(){ 
  int index = load_store_status.streamin.index.word*8;
  // hwpe_instance_->trace.msg(" control >>  store word index = %d\n", index);
  return index;// unused for depthwise mode
}

int GetStreaminWordSize(){ 
  int index = load_store_status.streamin.count.word;
  // hwpe_instance_->trace.msg(" control >>  store word size = %d\n", index);
  return index;// unused for depthwise mode
}

int StreaminLoadWidth(){
  int current_tile_sizekout_rem = (current_tile_size.kout & 0x7) ? (current_tile_size.kout >> 3)  : 8;
  int current_tile_sizekout_quo = (current_tile_size.kout & 0x7) ? 1 + (current_tile_size.kout>>3) : (current_tile_size.kout>>3);
  if(ctrl_config_.config0.streamin_bit_count==32)
    if(load_store_status.streamin.index.word < load_store_status.streamin.count.word-1) return L1BandwidthInBytes;
    else return (current_tile_sizekout_rem<<2);
  else return current_tile_size.kout;
}

StreamerConfig GetInFeatLoadStreamerConfig(){
  const auto& is_broadcast = ctrl_config_.config0.broadcast;
  const auto& filter_mode = ctrl_config_.config0.filter_mode;

  //addr_kin calculation
  const auto k_index = filter_mode == Depthwise ? prefetch_tiles.index.kout :
      prefetch_tiles.index.kin;
  const AddrType addr_kin = is_broadcast ? k_index :
      k_index * NeurekaInFeatScalarBufferCount;

  const auto k_total = is_broadcast ? 1 :
      filter_mode == Depthwise ? total_size.kout : total_size.kin;

  //addr_win calculation
  const AddrType addr_win = prefetch_tiles.index.win * NeurekaPECountX * k_total;

  //addr_hin calculation
  const auto w_total = total_size.win - ctrl_config_.padding.left - ctrl_config_.padding.right;
  const AddrType addr_hin = prefetch_tiles.index.hin * NeurekaPECountY * w_total * k_total;

  StreamerConfig infeat;
  infeat.base_addr = ctrl_config_.infeat_ptr + addr_hin + addr_win + addr_kin;
  infeat.stride.d0 = ctrl_config_.infeat_stride.d0;
  infeat.stride.d1 = ctrl_config_.infeat_stride.d1;
  infeat.stride.d2 = ctrl_config_.infeat_stride.d2;
  infeat.length.d0 = load_store_status.infeat.count.win;
  infeat.length.d1 = load_store_status.infeat.count.hin;
  infeat.length.d2 = 1; // unused
  return infeat;
}

StreamerConfig GetWeightLoadStreamerConfig(){
  Mode mode = ctrl_config_.config0.filter_mode;
  int qw = ctrl_config_.config0.weight_bit_count;

  StreamerConfig weight;
  const AddrType filter_size = mode == Pointwise ? 1*1 : 3*3;
  const AddrType kin_tile_stride = qw * filter_size * NeurekaColumnPerPECount / 8;

  if (mode != Depthwise) {
    const AddrType kout_tile_stride = NeurekaAccumulatorPerPECount * ctrl_config_.kin_tile_count * kin_tile_stride;
    weight.base_addr = ctrl_config_.weight_ptr + tiles.index.kout * kout_tile_stride + tiles.index.kin * kin_tile_stride;
  } else {
    const AddrType kout_tile_stride = kin_tile_stride;
    weight.base_addr = ctrl_config_.weight_ptr + tiles.index.kout * kout_tile_stride;
  }

  weight.stride.d0 = ctrl_config_.weight_stride.d0;
  weight.stride.d1 = ctrl_config_.weight_stride.d1;
  weight.stride.d2 = ctrl_config_.weight_stride.d2;
  weight.length.d0 = mode == Pointwise ? 1 : ctrl_config_.config0.weight_bit_count ;
  weight.length.d1 = mode == Depthwise ? 1 : current_tile_size.kout;
  weight.length.d2 = 1; // unused
  return weight;
}

// 0->0    ((0>>1)*3)
// 1->2 -> ((1-1)>>1)*3+2
// 2->3 -> (2>>1)*3
// 3->5 -> ((3-1)>>1)*3+2
// 4->6 -> (4>>1)*3
// 5->8 -> ((5-1)>>1)*3+2
// 6->9 -> (6>>1)*3
// 7->11 -> ((7-1)>>1)*3+2


// 0->0 -> (0>>1)*5  
// 1->3 -> ((1-1)>>1)*5+3
// 2->5 -> (2>>1)*5
// 3->8 -> ((3-1)>>1)*5+3
// 4->10 -> (4>>1)*5
// 5->13 -> ((5-1)>>1)*5+3
// 6->15 -> (6>>1)*5
// 7->18 -> ((7-1)>>1)*5+3



StreamerConfig GetOutFeatStoreStreamerConfig(){
  // int offs = ctrl_config_.config0.strided2x2 ? 2 : 1;
  int offs = ctrl_config_.config0.strided2x2 ? 1 : 0;
  // int pe_offs_h = (NeurekaPECountX%2==1)&&(NeurekaPECountY%2==1) && offs && (prev_tiles.index.hout%2 == 0)? 1 : 0;
  // int pe_offs_w = (NeurekaPECountX%2==1)&&(NeurekaPECountY%2==1) && offs && (prev_tiles.index.wout%2 == 0)? 1 : 0;
  int h_size = ctrl_config_.config0.strided2x2==0 ? ctrl_config_.outfeat_stride.d2 : total_size.kout*(total_size.wout & offs ? (1+total_size.wout)>>offs : total_size.wout>>offs);
  AddrType addr_kout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * prev_tiles.index.kout*NeurekaAccumulatorPerPECount;
  AddrType addr_wout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * prev_tiles.index.wout*((NeurekaPECountX)>>offs)*ctrl_config_.outfeat_stride.d1;

  // odd w_index => (PEIndexX+1)>>2 *  
  // even w_index => (w_index>>1)*NeurekaPE
  AddrType addr_hout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * prev_tiles.index.hout*((NeurekaPECountY)>>offs)*h_size;

  if((NeurekaPECountX%2==1)&&(NeurekaPECountY%2==1) && offs)
  {
    // std::cout<<"*************************************\n";
    // std::cout<<(((prev_tiles.index.wout-1)>>1)*NeurekaPECountX + (NeurekaPECountX+1)>>1)<<"\n";
    std::cout<<ctrl_config_.outfeat_stride.d1<<"\n";
    if(prev_tiles.index.wout%2 == 0)
      addr_wout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * (prev_tiles.index.wout>>1)*NeurekaPECountX*ctrl_config_.outfeat_stride.d1;
    else 
      addr_wout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * (((prev_tiles.index.wout-1)>>1)*NeurekaPECountX + ((NeurekaPECountX+1)>>1))*ctrl_config_.outfeat_stride.d1;


    if(prev_tiles.index.hout%2 == 0)
      addr_hout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * (prev_tiles.index.hout>>1)*NeurekaPECountY*h_size;
    else 
      addr_hout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * (((prev_tiles.index.hout-1)>>1)*NeurekaPECountY + ((NeurekaPECountY+1)>>1))*h_size;
    
  }

  // if(pe_offs_h)
  // {
  //   // ctrl_config_.config0.strided2x2==0 ? ctrl_config_.outfeat_stride.d2 : total_size.kout*(total_size.wout & offs ? (1+total_size.wout)>>offs : total_size.wout>>offs);
  //   addr_hout = (ctrl_config_.config0.quantization_bit_count == 32 ? 4 : 1) * prev_tiles.index.hout*((NeurekaPECountY))*h_size;
  // }
  // std::cout<<"addr_hout = "<<addr_hout<<"\n";
  // std::cout<<"addr_kout = "<<addr_kout<<"\n";
  // std::cout<<"addr_wout = "<<addr_wout<<"\n";
  // std::cout<<"index_hout = "<<prev_tiles.index.hout<<"\n";
  // std::cout<<"index_wout = "<<prev_tiles.index.wout<<"\n";
  // std::cout<<"index_kout = "<<prev_tiles.index.kout<<"\n";
  // std::cout<<"pe_offs_h = "<<pe_offs_h<<"\n";
  // std::cout<<"pe_offs_w = "<<pe_offs_w<<"\n";
  // std::cout<<"h_size = "<<h_size<<"\n";
  // hwpe_instance_->trace.msg(" control >> GetOutFeatStoreStreamerConfig INDEXES (kin : %d, kout : %d, hout : %d, wout : %d)\n", prev_tiles.index.kin, prev_tiles.index.kout, prev_tiles.index.hout, prev_tiles.index.wout);
  // hwpe_instance_->trace.msg(" control >> GetOutFeatStoreStreamerConfig COUNTS  (kin : %d, kout : %d, hout : %d, wout : %d)\n", prev_tiles.count.kin, prev_tiles.count.kout, prev_tiles.count.hout, prev_tiles.count.wout);
  // hwpe_instance_->trace.msg(" control >> GetOutFeatStoreStreamerConfig DONE    (kin : %d, kout : %d, hout : %d, wout : %d)\n", prev_tiles.done.kin, prev_tiles.done.kout, prev_tiles.done.hout, prev_tiles.done.wout);

  StreamerConfig outfeat;
  outfeat.base_addr = ctrl_config_.outfeat_ptr + addr_kout + addr_wout + addr_hout;

  // std::cout<<"outfeat.base_addr = "<<outfeat.base_addr<<"\n";

  outfeat.stride.d0 = ctrl_config_.outfeat_stride.d0;
  outfeat.stride.d1 = ctrl_config_.outfeat_stride.d1;
  outfeat.stride.d2 = ctrl_config_.outfeat_stride.d2;
  outfeat.length.d0 = load_store_status.outfeat.count.word;
  int hout_tile_odd = prev_tiles.index.hout % 2;
  int wout_tile_odd = prev_tiles.index.wout % 2;
  if(ctrl_config_.config0.strided2x2==0){
    outfeat.length.d1 = load_store_status.outfeat.count.wout;
    outfeat.length.d2 = load_store_status.outfeat.count.hout;
  } 
  else 
  {
    if(NeurekaPECountX %2 == 0 && NeurekaPECountY %2 == 0)
    {
      outfeat.length.d1 = load_store_status.outfeat.count.wout & offs ? (1+load_store_status.outfeat.count.wout)>>1 : load_store_status.outfeat.count.wout>>1;
      outfeat.length.d2 = load_store_status.outfeat.count.hout & offs ? (1+load_store_status.outfeat.count.hout)>>1 : load_store_status.outfeat.count.hout>>1; // unused
    }
    else if((NeurekaPECountX %2) == 1 && (NeurekaPECountY %2 == 1))
    {
      if(hout_tile_odd && wout_tile_odd){
        outfeat.length.d1 = load_store_status.outfeat.count.wout & offs ? (load_store_status.outfeat.count.wout)>>1 : (1+load_store_status.outfeat.count.wout)>>1;
        outfeat.length.d2 = load_store_status.outfeat.count.hout & offs ? (load_store_status.outfeat.count.hout)>>1 : (1+load_store_status.outfeat.count.hout)>>1; // unused
      } 
      else if(!hout_tile_odd && (wout_tile_odd)){
        outfeat.length.d1 = load_store_status.outfeat.count.wout & offs ? (load_store_status.outfeat.count.wout)>>1 : (1+load_store_status.outfeat.count.wout)>>1;
        outfeat.length.d2 = load_store_status.outfeat.count.hout & offs ? (1+load_store_status.outfeat.count.hout)>>1 : (load_store_status.outfeat.count.hout)>>1; // unused
      } 
      else if(hout_tile_odd && (!wout_tile_odd)){
        outfeat.length.d1 = load_store_status.outfeat.count.wout & offs ? (1+load_store_status.outfeat.count.wout)>>1 : (load_store_status.outfeat.count.wout)>>1;
        outfeat.length.d2 = load_store_status.outfeat.count.hout & offs ? (load_store_status.outfeat.count.hout)>>1 : (1+load_store_status.outfeat.count.hout)>>1; // unused
      } 
      else {
        outfeat.length.d1 = load_store_status.outfeat.count.wout & offs ? (1+load_store_status.outfeat.count.wout)>>1 : (load_store_status.outfeat.count.wout)>>1;
        outfeat.length.d2 = load_store_status.outfeat.count.hout & offs ? (1+load_store_status.outfeat.count.hout)>>1 : (load_store_status.outfeat.count.wout)>>1; // unused
      }
    } 
    else
    {
      std::cerr<<"unsupported \n";
    }
  }
  return outfeat;
}

void ResetStreaminIteration(){
  load_store_status.streamin.index.word = 0;
  load_store_status.streamin.index.hout = 0;
  load_store_status.streamin.index.wout = 0;
}
void StreaminIteration(){
  int word_index = load_store_status.streamin.index.word;
  int wout_index = load_store_status.streamin.index.wout;
  int hout_index = load_store_status.streamin.index.hout;
  int word_count = load_store_status.streamin.count.word - 1;
  int hout_count = load_store_status.streamin.count.hout - 1;
  int wout_count = load_store_status.streamin.count.wout - 1;

  load_store_status.streamin.index.word = (word_index  < word_count) ? 1 + load_store_status.streamin.index.word : 0;
  load_store_status.streamin.index.wout = (word_index == word_count) && (wout_index < wout_count) ? 1 + load_store_status.streamin.index.wout : (word_index == word_count) && (wout_index == wout_count) ? 0 : load_store_status.streamin.index.wout;
  load_store_status.streamin.index.hout = (word_index == word_count) && (wout_index == wout_count) && (hout_index < hout_count) ? 1 + load_store_status.streamin.index.hout : (word_index == word_count) && (wout_index == wout_count) && (hout_index == hout_count) ? 0 : load_store_status.streamin.index.hout;
  load_store_status.streamin.done       = (word_index == word_count) && (wout_index == wout_count) && (hout_index == hout_count) ? true : false;

  // hwpe_instance_->trace.msg(" control >>  Streamin Indexes ( word : %d, wout : %d, wout : %d ), Counts ( word : %d, wout : %d, hout : %d ) done : %d\n", load_store_status.streamin.index.word, load_store_status.streamin.index.wout, load_store_status.streamin.index.hout, load_store_status.streamin.count.word, load_store_status.streamin.count.wout, load_store_status.streamin.count.hout, load_store_status.streamin.done);
}
StreamerConfig GetStreaminStreamerConfig(){
  AddrType addr_kout = (ctrl_config_.config0.streamin_bit_count == 32 ? 4 : 1) * tiles.index.kout*NeurekaAccumulatorPerPECount;
  AddrType addr_wout = (ctrl_config_.config0.streamin_bit_count == 32 ? 4 : 1) * tiles.index.wout*NeurekaPECountX*total_size.kout;
  AddrType addr_hout = (ctrl_config_.config0.streamin_bit_count == 32 ? 4 : 1) * tiles.index.hout*NeurekaPECountX*total_size.wout*total_size.kout;

  // hwpe_instance_->trace.msg(" control >> GetOutFeatStoreStreamerConfig INDEXES (kin : %d, kout : %d, hout : %d, wout : %d)\n", prev_tiles.index.kin, prev_tiles.index.kout, prev_tiles.index.hout, prev_tiles.index.wout);
  // hwpe_instance_->trace.msg(" control >> GetOutFeatStoreStreamerConfig COUNTS  (kin : %d, kout : %d, hout : %d, wout : %d)\n", prev_tiles.count.kin, prev_tiles.count.kout, prev_tiles.count.hout, prev_tiles.count.wout);
  // hwpe_instance_->trace.msg(" control >> GetOutFeatStoreStreamerConfig DONE    (kin : %d, kout : %d, hout : %d, wout : %d)\n", prev_tiles.done.kin, prev_tiles.done.kout, prev_tiles.done.hout, prev_tiles.done.wout);

  int quant_bits = ctrl_config_.config0.quantization_bit_count;
  int streamin_bits = ctrl_config_.config0.streamin_bit_count;
  StreamerConfig streamin;
  streamin.base_addr = ctrl_config_.streamin_ptr + addr_kout + addr_wout + addr_hout;
  streamin.stride.d0 = ctrl_config_.outfeat_stride.d0;
  streamin.stride.d1 = quant_bits==streamin_bits ? ctrl_config_.outfeat_stride.d1 : (quant_bits==32 && streamin_bits==8) ? (ctrl_config_.outfeat_stride.d1>>2) : (ctrl_config_.outfeat_stride.d1<<2);
  streamin.stride.d2 = quant_bits==streamin_bits ? ctrl_config_.outfeat_stride.d2 : (quant_bits==32 && streamin_bits==8) ? (ctrl_config_.outfeat_stride.d2>>2) : (ctrl_config_.outfeat_stride.d2<<2);
  streamin.length.d0 = load_store_status.streamin.count.word;
  streamin.length.d1 = load_store_status.streamin.count.wout;
  streamin.length.d2 = load_store_status.streamin.count.hout; // unused
  return streamin;
}

void ResetNormQuantMultIteration(){tiles.index.norm_quant_mult = 0;}
void ResetNormQuantShiftIteration(){tiles.index.norm_quant_shift = 0;}
void ResetNormQuantBiasIteration(){tiles.index.norm_quant_bias = 0;}

bool NormQuantMultIteration(){
  tiles.index.norm_quant_mult++; 
  if(ctrl_config_.config0.normalization_bit_count == 32){
    tiles.count.norm_quant_mult = (current_tile_size.kout & 0x7) ? 1+ (current_tile_size.kout>>3) : (current_tile_size.kout >> 3);
  } else {
    tiles.count.norm_quant_mult = 1;
  }
  if(tiles.index.norm_quant_mult==tiles.count.norm_quant_mult)
    return true;
  else 
    return false;
}
bool NormQuantShiftIteration(){
  tiles.index.norm_quant_shift++;
  if(tiles.index.norm_quant_shift==tiles.count.norm_quant_shift)
    return true;
  else 
    return false;
}
bool NormQuantBiasIteration(){
  tiles.index.norm_quant_bias++;
  tiles.count.norm_quant_bias = (current_tile_size.kout & 0x7) ? 1+ (current_tile_size.kout>>3) : (current_tile_size.kout>>3);
  // std::cout<<"index="<<tiles.index.norm_quant_bias<<", count="<<tiles.count.norm_quant_bias<<"\n";
  if(tiles.index.norm_quant_bias==tiles.count.norm_quant_bias)
    return true;
  else 
    return false;
}

int GetNormQuantMultWidth(){
  if(ctrl_config_.config0.normalization_bit_count == 8)
    return current_tile_size.kout;
  else {
    if(tiles.index.norm_quant_mult==tiles.count.norm_quant_mult-1)
      return  ((current_tile_size.kout-(tiles.index.norm_quant_mult<<3))<<2);
    else
      return 32;
  }
}

int GetNormQuantBiasWidth(){
  tiles.count.norm_quant_bias = (current_tile_size.kout & 0x7) ? 1+ (current_tile_size.kout>>3) : (current_tile_size.kout>>3);
  if(tiles.index.norm_quant_bias==tiles.count.norm_quant_bias-1)
    return  (current_tile_size.kout-(tiles.index.norm_quant_bias<<3))<<2;
  else
    return 32;
}

int GetNormQuantShiftWidth(){
  return  current_tile_size.kout;
}


StreamerConfig GetNormquantMultStreamerConfig(){
  AddrType addr_kout = (ctrl_config_.config0.normalization_bit_count == 32 ? 4 : 1) * prev_tiles.index.kout*NeurekaAccumulatorPerPECount;
  StreamerConfig normquant_mult;
  normquant_mult.base_addr = ctrl_config_.scale_ptr + addr_kout;
  normquant_mult.stride.d0 = 32; // if it is 4 bits then 1 iteration is enough
  normquant_mult.stride.d1 = 0;// unused
  normquant_mult.stride.d2 = 0;//unused
  int num_iters = (current_tile_size.kout & 0x7) ? 1 + (current_tile_size.kout>>3) : (current_tile_size.kout>>3);
  normquant_mult.length.d0 = ctrl_config_.config0.normalization_bit_count==32 ? num_iters : 1;
  normquant_mult.length.d1 = 1;
  normquant_mult.length.d2 = 1;
  return normquant_mult;
}

StreamerConfig GetNormquantBiasStreamerConfig(){
  AddrType addr_kout = 4*prev_tiles.index.kout*NeurekaAccumulatorPerPECount;// always 32bits
  StreamerConfig normquant_bias;
  normquant_bias.base_addr = ctrl_config_.scale_bias_ptr + addr_kout;
  normquant_bias.stride.d0 = 32; // if it is 4 bits then 1 iteration is enough
  normquant_bias.stride.d1 = 0;// unused
  normquant_bias.stride.d2 = 0;//unused
  int num_iters = (current_tile_size.kout & 0x7) ? 1 + (current_tile_size.kout>>3) : (current_tile_size.kout>>3);
  // std::cout<<"num iters="<<num_iters<<"\n";
  // std::cout<<"current_tile_size.kout="<<current_tile_size.kout<<"\n";   
  normquant_bias.length.d0 = num_iters;
  normquant_bias.length.d1 = 1;
  normquant_bias.length.d2 = 1;
  return normquant_bias;
}

StreamerConfig GetNormquantShiftStreamerConfig(){
  AddrType addr_kout = prev_tiles.index.kout*NeurekaAccumulatorPerPECount;
  StreamerConfig normquant_shift;
  normquant_shift.base_addr = ctrl_config_.scale_shift_ptr + addr_kout;
  normquant_shift.stride.d0 = 32; // if it is 4 bits then 1 iteration is enough
  normquant_shift.stride.d1 = 0;// unused
  normquant_shift.stride.d2 = 0;//unused
  normquant_shift.length.d0 = 1;
  normquant_shift.length.d1 = 1;
  normquant_shift.length.d2 = 1;
  return normquant_shift;
}


bool isPadding() {
  const int h_index = load_store_status.infeat.index.hin;
  const int w_index = load_store_status.infeat.index.win;
  const int h_count = load_store_status.infeat.count.hin;
  const int w_count = load_store_status.infeat.count.win;
  const Padding padding = ctrl_config_.padding;

  // TODO: Should the prefetch conditions also check for the value of padding or be hardcoded like this?
  return ((w_index < padding.left && prefetch_tiles.index.win == 0) ||
          (h_index < padding.top  && prefetch_tiles.index.hin == 0) ||
          (w_index >= w_count - padding.right  && prefetch_tiles.index.win == prefetch_tiles.count.win - 1) ||
          (h_index >= h_count - padding.bottom && prefetch_tiles.index.hin == prefetch_tiles.count.hin - 1));
}

};

#endif
