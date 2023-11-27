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
 * Authors: Francesco Conti, University of Bologna & GreenWaves Technologies (f.conti@unibo.it)
 *          Arpan Suravi Prasad, ETH Zurich (prasadar@iis.ee.ethz.ch)
 */
#ifndef JOB_HANDLE_H
#define JOB_HANDLE_H
#include "datatype.hpp"
#include "params.hpp"
template <typename HwpeType>
class RegConfigManager{

  public:
    int** ctxt_;
    int ctxt_count_;
    int register_count_;
    int* ctxt_job_id_;
    int  ctxt_cfg_ptr_;
    int  ctxt_use_ptr_;
    int  job_pending_;
    int  job_state_;
    unsigned char job_id_;
    char running_job_id_;
    int  job_running_;
    RegConfig reg_config_; 
    NeurekaTraceLevel trace_level;
    int trace_format;

    private:
    HwpeType* hwpe_instance_;

  public:
    RegConfigManager(){};
    RegConfigManager(HwpeType* hwpe, int RegisterContextCount, int RegisterCount) {
      hwpe_instance_ = hwpe;
      ctxt_count_ = RegisterContextCount;
      register_count_ = RegisterCount;
      ctxt_ = new int*[ctxt_count_];
      ctxt_job_id_ = new int[ctxt_count_];
      ctxt_cfg_ptr_ = 0;
      for(int i=0; i<ctxt_count_; i++) {
        ctxt_[i] = new int[register_count_];
        ctxt_job_id_[i] = 0;
      }
    }

  int RegfileRead(int addr) {
  if(addr == NEUREKA_SPECIAL_TRACE_REG) {
    if(trace_level == L0_CONFIG) {
      return 0;
    }
    else if(trace_level == L1_ACTIV_INOUT) {
      return 1;
    }
    else if(trace_level == L2_DEBUG) {
      return 2;
    }
    else {
      return 3;
    }
  }
  else if(addr < register_count_) {
    if (ctxt_cfg_ptr_ == 0) {
      return ctxt_[0][addr];
    }
    else {
      return ctxt_[1][addr];
    }
  }
  else if (addr < 2*register_count_) {
    return ctxt_[0][addr - register_count_];
  }
  else {
    return ctxt_[1][addr - 2*register_count_];
  }
}

void RegfileWrite(int addr, int value) {
  if(addr == NEUREKA_SPECIAL_TRACE_REG) {
    if(value == 0) {
      trace_level = L0_CONFIG;
    }
    else if(value == 1) {
      trace_level = L1_ACTIV_INOUT;
    }
    else if(value == 2) {
      trace_level = L2_DEBUG;
    }
    else {
      trace_level = L3_ALL;
    }
  }
  else if (addr == NEUREKA_SPECIAL_FORMAT_TRACE_REG) {
    trace_format = value;
  }
  else if(addr < register_count_) {

    if (ctxt_cfg_ptr_ == 0) {
      ctxt_[0][addr] = value;

    }
    else if(ctxt_cfg_ptr_ == 1) {
  
      ctxt_[1][addr] = value;
    }
  }
  else if (addr < 2*register_count_) {
    ctxt_[0][addr - register_count_] = value;
  }
  else {
    ctxt_[1][addr - 2*register_count_] = value;
  }
}

void RegfileCtxt() {

  for(auto addr=0; addr<register_count_; addr++) {

    auto value = ctxt_use_ptr_ == 0 ? ctxt_[0][addr] : ctxt_[1][addr];

    switch(addr) {

      case NEUREKA_REG_WEIGHTS_PTR:
        reg_config_.weight_ptr = value;
        break;

      case NEUREKA_REG_INFEAT_PTR:
        reg_config_.infeat_ptr = value;
        break;

      case NEUREKA_REG_OUTFEAT_PTR:
        reg_config_.outfeat_ptr = value;
        break;

      case NEUREKA_REG_SCALE_PTR:
        reg_config_.scale_ptr = value;
        break;

      case NEUREKA_REG_SCALE_SHIFT_PTR:
        reg_config_.scale_shift_ptr = value;
        break;

      case NEUREKA_REG_SCALE_BIAS_PTR:
        reg_config_.scale_bias_ptr = value;
        break;

      case NEUREKA_REG_STREAMIN_PTR:
        reg_config_.streamin_ptr = value;
        break;

      case NEUREKA_REG_INFEAT_D0_STRIDE:
        reg_config_.infeat_stride.d0 = value;
        break;

      case NEUREKA_REG_INFEAT_D1_STRIDE:
        reg_config_.infeat_stride.d1 = value;
        break;

      case NEUREKA_REG_INFEAT_D2_STRIDE:
        reg_config_.infeat_stride.d2 = value;
        break;

      case NEUREKA_REG_OUTFEAT_D0_STRIDE:
        reg_config_.outfeat_stride.d0 = value;
        break;

      case NEUREKA_REG_OUTFEAT_D1_STRIDE:
        reg_config_.outfeat_stride.d1 = value;
        break;

      case NEUREKA_REG_OUTFEAT_D2_STRIDE:
        reg_config_.outfeat_stride.d2 = value;
        break;

      case NEUREKA_REG_WEIGHTS_D0_STRIDE:
        reg_config_.weight_stride.d0 = value;
        break;

      case NEUREKA_REG_WEIGHTS_D1_STRIDE:
        reg_config_.weight_stride.d1 = value;
        break;

      case NEUREKA_REG_WEIGHTS_D2_STRIDE:
        reg_config_.weight_stride.d2 = value;
        break;

      case NEUREKA_REG_SUBTILE_REM0:
        reg_config_.kout_tile_rem = (value >> 16) & 0xffff;
        reg_config_.kin_tile_rem = value & 0xffff;
        break;

      case NEUREKA_REG_SUBTILE_REM1:
        reg_config_.hout_tile_rem = (value >> 16) & 0xffff;
        reg_config_.wout_tile_rem = value & 0xffff;
        break;

      case NEUREKA_REG_SUBTILE_REM2:
        reg_config_.hin_tile_rem = (value >> 16) & 0xffff;
        reg_config_.win_tile_rem = value & 0xffff;
        break;

      case NEUREKA_REG_SUBTILE_NB0:
        reg_config_.kout_tile_count = (value >> 16) & 0xffff;
        reg_config_.kin_tile_count = value & 0xffff;
        break;

      case NEUREKA_REG_SUBTILE_NB1:
        reg_config_.hout_tile_count = (value >> 16) & 0xffff;
        reg_config_.wout_tile_count = value & 0xffff;
        break;

      case NEUREKA_REG_PADDING:
        reg_config_.padding.top = (value >> 28) & 0xf;
        reg_config_.padding.right= (value >> 24) & 0xf;
        reg_config_.padding.bottom  = (value >> 20) & 0xf;
        reg_config_.padding.left = (value >> 16) & 0xf;
        reg_config_.padding.value  = value & 0xffff;
        break;

      case NEUREKA_REG_WEIGHT_OFFSET:
        reg_config_.Wmin = value;
        break;

      case NEUREKA_REG_FILTER_MASK:
        for(int row=0; row<9; row++)
          reg_config_.filter_mask_bit[row] = (((value) & 0x1FF)>>(8-row))&(0x1);//<----------FIX ME
        break;

      case NEUREKA_REG_CONFIG0:
        reg_config_.config0.broadcast = ((value >> 30) & 0x1) ? true : false;
        // [26] signed_activation 
        reg_config_.config0.residual = ((value >> 29) & 0x1) ? true : false;
        reg_config_.config0.signed_outfeat = ((value >> 28) & 0x1) ? true : false; // only useful for 8bit streamin
        reg_config_.config0.signed_streamin = ((value >> 27) & 0x1) ? true : false; // only useful for 8bit streamin
        reg_config_.config0.signed_activation = ((value >> 26) & 0x1) ? true : false;
        // [25] norm option bias (0=do not load bias; 1=load bias)
        reg_config_.config0.norm_option_bias = ((value >> 25) & 0x1) ? true : false;
        // [24] norm option shift (0=use quantization right shift; 1=load with norm)
        reg_config_.config0.norm_option_shift = ((value >> 24) & 0x1) ? true : false;
        // [23] quantization rect(0=rectify + consider as unsigned; 1=do not rectify, keep sign)
        reg_config_.config0.use_relu = ((value >> 23) & 0x1) ? false : true;
        // [22:21] quantization bits (00=8-bits, 01=16-bits, 10=32-bits)
        reg_config_.config0.quantization_bit_count = 8 << ((value >> 21) & 0x3);
        // [20:16] quantization right shift
        reg_config_.config0.quantization_right_shift = (value >> 16) & 0x1f;
        // [15] weight offset cfg (0=symmetric weights, 1=use layer-wise weight_offset)
          // FIXME not implemented
        reg_config_.config0.streamin_bit_count = ((value >> 15) & 0x1) ? 32 : 8;
        // [14] streamin mode (0=normal operation, 1=perform streamin)
        reg_config_.config0.streamin = ((value >> 14) & 0x1) ? true : false;
        // [13:12] normalization bits (00=8-bits, 01=16-bits, 10=32-bits)
        reg_config_.config0.normalization_bit_count = 8 << ((value >> 12) & 0x3);
        // [10] activation_prefetch -> if set to 1, activation is prefetched for the next tile.
        reg_config_.config0.infeat_prefetch = ((value >> 10) & 0x1) ? true : false; 
        // [9] weight_demux -> if set to 1, weight is fetched from the wmem else it is fetched from L1
        reg_config_.config0.weight_from_wmem       = ((value >> 9) & 0x1) ? true : false; 
        // [8] strided 2x2 mode (0=normal operation, 1=strided mode)
        reg_config_.config0.strided2x2 = ((value >> 8) & 0x1) ? true : false; // Check the config regs
        // [7] linear mode (0=normal operation, 1=linear mode)
        reg_config_.config0.filter_size = ((value >> 5) & 0x3) <= 1 ? 3 : 1;
        reg_config_.config0.depthwise_mode = ((value >> 5) & 0x3) == 1 ? true : false;
        if(reg_config_.config0.filter_size == 3 && reg_config_.config0.depthwise_mode==true)
          reg_config_.config0.filter_mode = Depthwise;
        else if(reg_config_.config0.filter_size == 3 && reg_config_.config0.depthwise_mode==false)
          reg_config_.config0.filter_mode = Dense3x3;
        else if(reg_config_.config0.filter_size == 1 && reg_config_.config0.depthwise_mode==false)
          reg_config_.config0.filter_mode = Pointwise;
        else 
          hwpe_instance_->trace.fatal("regconfig_manager >> unsupported filter mode");
        // [4] streamout / quantization (1=quantization+streamout, 0=streamout only)
        reg_config_.config0.outfeat_quant = ((value >> 4) & 0x1) ? true : false;
        // [2:0] weight bits minus 1.
        reg_config_.config0.weight_bit_count = (value & 0x7) + 1;
        break;

    }
  }
}
void Commit() {
  job_pending_++;
  job_state_ = 0;
  ctxt_cfg_ptr_ = 1-ctxt_cfg_ptr_;
}

int Acquire() {
  if(job_state_ == 0 & job_pending_ < 2) {
    int job_id_ = (int) job_id_++;
    ctxt_job_id_[ctxt_cfg_ptr_] = job_id_;
    job_state_ = -2;
    return job_id_;
  }
  else if(job_pending_ == 2) {
    return -1;
  }
  else {
    return job_state_;
  }
}

bool Status() {
  if(job_state_ == 0 & job_pending_ == 0)
    return false;
  else
    return true;
}
void PrintReg()
{
  std::ostringstream string_stream;
  string_stream<<"\n*************************  Configuration Values *****************************************\n";
  string_stream<<"regconfig_manager >> weight ptr : 0x"<<std::hex<<reg_config_.weight_ptr<<"\n";
  string_stream<<"regconfig_manager >> infeat ptr : 0x"<<std::hex<<reg_config_.infeat_ptr<<"\n";
  string_stream<<"regconfig_manager >> outfeat ptr : 0x"<<std::hex<<reg_config_.outfeat_ptr<<"\n";
  string_stream<<"regconfig_manager >> scale ptr : 0x"<<std::hex<<reg_config_.scale_ptr<<"\n";
  string_stream<<"regconfig_manager >> scale shift ptr : 0x"<<std::hex<<reg_config_.scale_shift_ptr<<"\n";
  string_stream<<"regconfig_manager >> scale bias ptr : 0x"<<std::hex<<reg_config_.scale_bias_ptr<<"\n";
  string_stream<<"regconfig_manager >> streamin ptr : 0x"<<std::hex<<reg_config_.streamin_ptr<<"\n";
  string_stream<<"regconfig_manager >> infeat d0 stride : 0x"<<std::hex<<reg_config_.infeat_stride.d0<<"\n";
  string_stream<<"regconfig_manager >> infeat d1 stride : 0x"<<std::hex<<reg_config_.infeat_stride.d1<<"\n";
  string_stream<<"regconfig_manager >> infeat d2 stride : 0x"<<std::hex<<reg_config_.infeat_stride.d2<<"\n";
  string_stream<<"regconfig_manager >> outfeat d0 stride : 0x"<<std::hex<<reg_config_.outfeat_stride.d0<<"\n";
  string_stream<<"regconfig_manager >> outfeat d1 stride : 0x"<<std::hex<<reg_config_.outfeat_stride.d1<<"\n";
  string_stream<<"regconfig_manager >> outfeat d2 stride : 0x"<<std::hex<<reg_config_.outfeat_stride.d2<<"\n";
  string_stream<<"regconfig_manager >> weight d0 stride : 0x"<<std::hex<<reg_config_.weight_stride.d0<<"\n";
  string_stream<<"regconfig_manager >> weight d1 stride : 0x"<<std::hex<<reg_config_.weight_stride.d1<<"\n";
  string_stream<<"regconfig_manager >> weight d2 stride : 0x"<<std::hex<<reg_config_.weight_stride.d2<<"\n";
  string_stream<<"regconfig_manager >> Wmin value : 0x"<<std::dec<<reg_config_.Wmin<<"\n";
  
  string_stream<<"regconfig_manager >> kin_tile_rem : "<<std::dec<<reg_config_.kin_tile_rem<<"\n";
  string_stream<<"regconfig_manager >> kout_tile_rem : "<<std::dec<<reg_config_.kout_tile_rem<<"\n";
  string_stream<<"regconfig_manager >> hin_tile_rem : "<<std::dec<<reg_config_.hin_tile_rem<<"\n";
  string_stream<<"regconfig_manager >> hout_tile_rem : "<<std::dec<<reg_config_.hout_tile_rem<<"\n";
  string_stream<<"regconfig_manager >> win_tile_rem : "<<std::dec<<reg_config_.win_tile_rem<<"\n";
  string_stream<<"regconfig_manager >> wout_tile_rem : "<<std::dec<<reg_config_.wout_tile_rem<<"\n";
  

  string_stream<<"regconfig_manager >> kin_tile_count : "<<std::dec<<reg_config_.kin_tile_count<<"\n";
  string_stream<<"regconfig_manager >> kout_tile_count : "<<std::dec<<reg_config_.kout_tile_count<<"\n";
  string_stream<<"regconfig_manager >> hout_tile_count : "<<std::dec<<reg_config_.hout_tile_count<<"\n";
  string_stream<<"regconfig_manager >> wout_tile_count : "<<std::dec<<reg_config_.wout_tile_count<<"\n";
  string_stream<<"regconfig_manager >> padding_top : "<<std::dec<<reg_config_.padding.top<<"\n";
  string_stream<<"regconfig_manager >> padding_bottom : "<<std::dec<<reg_config_.padding.bottom<<"\n";
  string_stream<<"regconfig_manager >> padding_left : "<<std::dec<<reg_config_.padding.left<<"\n";
  string_stream<<"regconfig_manager >> padding_right : "<<std::dec<<reg_config_.padding.right<<"\n";

  string_stream<<"regconfig_manager >> filter_mask : [";
  for(int i=0; i<9; i++)
    string_stream<<" "<<std::boolalpha<<reg_config_.filter_mask_bit[i];
  string_stream<<" ]\n";

  string_stream<<"regconfig_manager >> is_broadcast : "<<std::boolalpha<<reg_config_.config0.broadcast<<"\n";
  string_stream<<"regconfig_manager >> is_residual : "<<std::boolalpha<<reg_config_.config0.residual<<"\n";
  string_stream<<"regconfig_manager >> signed_outfeat : "<<std::boolalpha<<reg_config_.config0.signed_outfeat<<"\n";
  string_stream<<"regconfig_manager >> signed_streamin : "<<std::boolalpha<<reg_config_.config0.signed_streamin<<"\n";
  string_stream<<"regconfig_manager >> signed_activation : "<<std::boolalpha<<reg_config_.config0.signed_activation<<"\n";
  string_stream<<"regconfig_manager >> norm_option_bias : "<<std::boolalpha<<reg_config_.config0.norm_option_bias<<"\n";
  string_stream<<"regconfig_manager >> norm_option_shift : "<<std::boolalpha<<reg_config_.config0.norm_option_shift<<"\n";
  string_stream<<"regconfig_manager >> use_relu : "<<std::boolalpha<<reg_config_.config0.use_relu<<"\n";
  string_stream<<"regconfig_manager >> quantization_bit_count : "<<std::dec<<reg_config_.config0.quantization_bit_count<<"\n";
  string_stream<<"regconfig_manager >> quantization_right_shift : "<<std::boolalpha<<reg_config_.config0.quantization_right_shift<<"\n";
  string_stream<<"regconfig_manager >> streamin_bit_count : "<<std::dec<<reg_config_.config0.streamin_bit_count<<"\n";
  string_stream<<"regconfig_manager >> streamin : "<<std::boolalpha<<reg_config_.config0.streamin<<"\n";
  string_stream<<"regconfig_manager >> normalization_bit_count : "<<std::dec<<reg_config_.config0.normalization_bit_count<<"\n";
  string_stream<<"regconfig_manager >> infeat_prefetch : "<<std::boolalpha<<reg_config_.config0.infeat_prefetch<<"\n";
  string_stream<<"regconfig_manager >> weight_from_wmem : "<<std::boolalpha<<reg_config_.config0.weight_from_wmem<<"\n";
  string_stream<<"regconfig_manager >> strided2x2 : "<<std::boolalpha<<reg_config_.config0.strided2x2<<"\n";
  string_stream<<"regconfig_manager >> filter_size : "<<std::dec<<reg_config_.config0.filter_size<<"\n";
  string_stream<<"regconfig_manager >> depthwise_mode : "<<std::boolalpha<<reg_config_.config0.depthwise_mode<<"\n";
  string_stream<<"regconfig_manager >> filter_mode : "<<std::dec<<reg_config_.config0.filter_mode<<"\n";
  string_stream<<"regconfig_manager >> outfeat_quant : "<<std::boolalpha<<reg_config_.config0.outfeat_quant<<"\n";
  string_stream<<"regconfig_manager >> weight_bit_count : "<<std::dec<<reg_config_.config0.weight_bit_count<<"\n";
  string s = string_stream.str();
  hwpe_instance_->trace.msg(s.c_str());
}
};

#endif