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

#include <neureka.hpp>

int Neureka::regfile_rd(int addr) {
  if(addr == NEUREKA_SPECIAL_TRACE_REG) {
    if(this->trace_level == L0_CONFIG) {
      return 0;
    }
    else if(this->trace_level == L1_ACTIV_INOUT) {
      return 1;
    }
    else if(this->trace_level == L2_DEBUG) {
      return 2;
    }
    else {
      return 3;
    }
  }
  else if(addr < NEUREKA_NB_REG) {
    if (this->cxt_cfg_ptr == 0) {
      return this->cxt0[addr];
    }
    else {
      return this->cxt1[addr];
    }
  }
  else if (addr < 2*NEUREKA_NB_REG) {
    return this->cxt0[addr - NEUREKA_NB_REG];
  }
  else {
    return this->cxt1[addr - 2*NEUREKA_NB_REG];
  }
}

void Neureka::regfile_wr(int addr, int value) {
  if(addr == NEUREKA_SPECIAL_TRACE_REG) {
    if(value == 0) {
      this->trace_level = L0_CONFIG;
    }
    else if(value == 1) {
      this->trace_level = L1_ACTIV_INOUT;
    }
    else if(value == 2) {
      this->trace_level = L2_DEBUG;
    }
    else {
      this->trace_level = L3_ALL;
    }
  }
  else if (addr == NEUREKA_SPECIAL_FORMAT_TRACE_REG) {
    this->trace_format = value;
  }
  else if(addr < NEUREKA_NB_REG) {
    if (this->cxt_cfg_ptr == 0) {
      this->cxt0[addr] = value;
    }
    else if(this->cxt_cfg_ptr == 1) {
      this->cxt1[addr] = value;
    }
  }
  else if (addr < 2*NEUREKA_NB_REG) {
    this->cxt0[addr - NEUREKA_NB_REG] = value;
  }
  else {
    this->cxt1[addr - 2*NEUREKA_NB_REG] = value;
  }
}

void Neureka::regfile_cxt() {

  for(auto addr=0; addr<NEUREKA_NB_REG; addr++) {

    auto value = this->cxt_use_ptr == 0 ? this->cxt0[addr] : this->cxt1[addr];

    switch(addr) {

      case NEUREKA_REG_WEIGHTS_PTR:
        this->weights_ptr = value + 0x10400000;
        break;

      case NEUREKA_REG_INFEAT_PTR:
        this->infeat_ptr = value;
        break;

      case NEUREKA_REG_OUTFEAT_PTR:
        this->outfeat_ptr = value;
        break;

      case NEUREKA_REG_SCALE_PTR:
        this->scale_ptr = value;
        break;

      case NEUREKA_REG_SCALE_SHIFT_PTR:
        this->scale_shift_ptr = value;
        break;

      case NEUREKA_REG_SCALE_BIAS_PTR:
        this->scale_bias_ptr = value;
        break;

      case NEUREKA_REG_INFEAT_D0_STRIDE:
        this->infeat_d0_stride = value;
        break;

      case NEUREKA_REG_INFEAT_D1_STRIDE:
        this->infeat_d1_stride = value;
        break;

      case NEUREKA_REG_INFEAT_D2_STRIDE:
        this->infeat_d2_stride = value;
        break;

      case NEUREKA_REG_OUTFEAT_D0_STRIDE:
        this->outfeat_d0_stride = value;
        break;

      case NEUREKA_REG_OUTFEAT_D1_STRIDE:
        this->outfeat_d1_stride = value;
        break;

      case NEUREKA_REG_OUTFEAT_D2_STRIDE:
        this->outfeat_d2_stride = value;
        break;

      case NEUREKA_REG_WEIGHTS_D0_STRIDE:
        this->weights_d0_stride = value;
        break;

      case NEUREKA_REG_WEIGHTS_D1_STRIDE:
        this->weights_d1_stride = value;
        break;

      case NEUREKA_REG_WEIGHTS_D2_STRIDE:
        this->weights_d2_stride = value;
        break;

      case NEUREKA_REG_SUBTILE_REM0:
        this->subtile_rem_ko = (value >> 16) & 0xffff;
        this->subtile_rem_ki = value & 0xffff;
        break;

      case NEUREKA_REG_SUBTILE_REM1:
        this->subtile_rem_ho = (value >> 16) & 0xffff;
        this->subtile_rem_wo = value & 0xffff;
        break;

      case NEUREKA_REG_SUBTILE_REM2:
        this->subtile_rem_hi = (value >> 16) & 0xffff;
        this->subtile_rem_wi = value & 0xffff;
        break;

      case NEUREKA_REG_SUBTILE_NB0:
        this->subtile_nb_ko = (value >> 16) & 0xffff;
        this->subtile_nb_ki = value & 0xffff;
        break;

      case NEUREKA_REG_SUBTILE_NB1:
        this->subtile_nb_ho = (value >> 16) & 0xffff;
        this->subtile_nb_wo = value & 0xffff;
        break;

      case NEUREKA_REG_PADDING:
        this->padding_top    = (value >> 28) & 0xf;
        this->padding_right  = (value >> 24) & 0xf;
        this->padding_bottom = (value >> 20) & 0xf;
        this->padding_left   = (value >> 16) & 0xf;
        this->padding_value  = value & 0xffff;
        break;

      case NEUREKA_REG_WEIGHT_OFFSET:
        this->Wmin = value;
        break;

      case NEUREKA_REG_FILTER_MASK:
        // this->filter_mask_config = value;
        this->filter_mask_top    = ((value) & 0x1FF);//<----------FIX ME
        // this->filter_mask_right  = (value >> 16) & 0xff;
        // this->filter_mask_bottom = (value >> 8) & 0xff;
        // this->filter_mask_left   = value & 0xff;
        // this->filter_mask_top    = ((value) & 0b1) & (((value) & 0b10)>>1)  & (((value) & 0b100)>>2);
        // this->filter_mask_bottom    = (((value) & 0b1000000) >> 6) & (((value) & 0b10000000)>>7)  & (((value) & 0b100000000)>>8);
        // this->filter_mask_left    = ((value) & 0b1) & (((value) & 0b1000)>>3)  & (((value) & 0b1000000)>>6);
        // this->filter_mask_right    = (((value) & 0b100) >> 2) & (((value) & 0b100000)>>5)  & (((value) & 0b100000000)>>8);
        // std::cout<<"FILTER MASK TOP"<<this->filter_mask_top<<std::endl;
        // std::cout<<"FILTER MASK BOTTOM"<<this->filter_mask_bottom<<std::endl;
        // std::cout<<"FILTER MASK LEFT"<<this->filter_mask_left<<std::endl;
        // std::cout<<"FILTER MASK RIGHT"<<this->filter_mask_right<<std::endl;
        // std::cout<<"VALUE = "<<std::hex<<value<<std::endl;

        break;
/*
 cfg[NE16_REG_CONFIG0]           = bits(0, 31, 31) |
                                    bits(8, 27, 30) |
                                    bits(this->signed_activation ? 1:0, 26, 26) |
                                    bits(this->norm_option_bias ? 1 : 0, 25, 25) |
                                    bits(this->norm_option_shift ? 1 : 0, 24, 24) |
                                    bits(this->use_relu ? 0 : 1, 23, 23) |
                                    bits(this->quantization_bits >> 4, 21, 22) |
                                    bits(this->shift_reqnt(0), 16, 20) |
                                    bits(0, 15, 15) | // FIXME UNUSED
                                    bits(this->streamin ? 1 : 0, 14, 14) |
                                    bits(this->normalization_bits >> 4, 12, 13) |
                                    bits(this->use_rounding ? 0 : 1, 11, 11) |
                                    bits(0, 8, 10)  | // FIXME reserved for padding flags --> UNUSED
                                    bits(this->mode_linear ? 1 : 0, 7, 7) |
                                    bits(filter_mode, 5, 6) |
                                    bits(this->output_quant, 4, 4) |
                                    bits(this->mode16 ? 1 : 0, 3, 3) |
                                    bits(this->qw-1, 0, 2) |
                                    bits(this->activation_prefetch ? 1:0,10,10)|
                                    bits(this->weight_demux ? 1:0,9,9);// weight_demux

*/
      case NEUREKA_REG_CONFIG0:
        // [26] signed_activation
        this->signed_activation = ((value >> 26) & 0x1) ? true : false;
//         std::cout<<"SIGNED ACTIVATION="<<this->signed_activation<<"\n";
        // [25] norm option bias (0=do not load bias; 1=load bias)
        this->norm_option_bias = ((value >> 25) & 0x1) ? true : false;
        // [24] norm option shift (0=use quantization right shift; 1=load with norm)
        this->norm_option_shift = ((value >> 24) & 0x1) ? true : false;
        // [23] quantization rect(0=rectify + consider as unsigned; 1=do not rectify, keep sign)
        this->use_relu = ((value >> 23) & 0x1) ? false : true;
        // [22:21] quantization bits (00=8-bits, 01=16-bits, 10=32-bits)
        this->quantization_bits = 8 << ((value >> 21) & 0x3);
        // [20:16] quantization right shift
        this->quantization_right_shift = (value >> 16) & 0x1f;
        // [15] weight offset cfg (0=symmetric weights, 1=use layer-wise weight_offset)
          // FIXME not implemented
        // [14] streamin mode (0=normal operation, 1=perform streamin)
        this->streamin = ((value >> 14) & 0x1) ? true : false;
        // [13:12] normalization bits (00=8-bits, 01=16-bits, 10=32-bits)
        this->normalization_bits = 8 << ((value >> 12) & 0x3);
        // [10] activation_prefetch -> if set to 1, activation is prefetched for the next tile.
        this->activation_prefetch = ((value >> 10) & 0x1) ? true : false;
        // [9] weight_demux -> if set to 1, weight is fetched from the wmem else it is fetched from L1
        this->weight_demux       = ((value >> 9) & 0x1) ? true : false;
        // [8] strided 2x2 mode (0=normal operation, 1=strided mode)
        this->strided2x2 = ((value >> 8) & 0x1) ? true : false; // Check the config regs
        // [7] linear mode (0=normal operation, 1=linear mode)
        this->mode_linear = ((value >> 7) & 0x1) ? true : false;
        // [6:5] filter mode (11=reserved, 10=1x1, 01=3x3 depthwise, 00=3x3)
        this->fs = ((value >> 5) & 0x3) <= 1 ? 3 : 1;
        this->depthwise = ((value >> 5) & 0x3) == 1 ? true : false;
        // [4] streamout / quantization (1=quantization+streamout, 0=streamout only)
        this->output_quant = ((value >> 4) & 0x1) ? true : false;
        // [3] mode16 (0=8-bits mode; 1=16-bits mode)
        this->mode16 = ((value >> 3) & 0x1) ? true : false;
        // [2:0] weight bits minus 1.
        this->qw = (value & 0x7) + 1;
        break;

    }
  }
}

void Neureka::printout() {
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) TP_IN=%d\n", this->TP_IN);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) TP_IN_LINEAR=%d\n", this->TP_IN_LINEAR);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) TP_OUT=%d\n", this->TP_OUT);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) QA_IN=%d\n", this->QA_IN);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) QA_OUT=%d\n", this->QA_OUT);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) NR_COLUMN=%d\n", this->NR_COLUMN);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) COLUMN_SIZE=%d\n", this->COLUMN_SIZE);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) BLOCK_SIZE=%d\n", this->BLOCK_SIZE);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) F_BUFFER_SIZE=%d\n", this->F_BUFFER_SIZE);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) FILTER_SIZE=%d\n", this->FILTER_SIZE);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) SHIFT_CYCLES=%d\n", this->SHIFT_CYCLES);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) OVERHEAD_LD_1X1=%d\n", this->OVERHEAD_LD_1X1);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) OVERHEAD_LD_3X3=%d\n", this->OVERHEAD_LD_3X3);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) OVERHEAD_MV=%d\n", this->OVERHEAD_MV);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(archi) QUANT_PER_CYCLE=%d\n", this->QUANT_PER_CYCLE);
  // REGISTER FILE and HWPE CTRL
  this->trace.msg(vp::Trace::LEVEL_INFO, "(ctrl) cxt_cfg_ptr=%d\n", this->cxt_cfg_ptr);
  this->trace.msg(vp::Trace::LEVEL_INFO, "(ctrl) cxt_use_ptr=%d\n", this->cxt_use_ptr);
  this->trace.msg(vp::Trace::LEVEL_INFO, "(ctrl) job_pending=%d\n", this->job_pending);
  this->trace.msg(vp::Trace::LEVEL_INFO, "(ctrl) job_state=%d\n", this->job_state);
  this->trace.msg(vp::Trace::LEVEL_INFO, "(ctrl) job_id=%d\n", this->job_id);
  // REGISTER FILE configuration parameters
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) weights_ptr=%p\n", this->weights_ptr); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) infeat_ptr=%p\n", this->infeat_ptr); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) outfeat_ptr=%p\n", this->outfeat_ptr); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) scale_ptr=%p\n", this->scale_ptr); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) scale_shift_ptr=%p\n", this->scale_shift_ptr); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) scale_bias_ptr=%p\n", this->scale_bias_ptr); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) infeat_d0_stride=%d\n", this->infeat_d0_stride); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) infeat_d1_stride=%d\n", this->infeat_d1_stride); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) infeat_d2_stride=%d\n", this->infeat_d2_stride); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) weights_d0_stride=%d\n", this->weights_d0_stride); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) weights_d1_stride=%d\n", this->weights_d1_stride); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) weights_d2_stride=%d\n", this->weights_d2_stride); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) outfeat_d0_stride=%d\n", this->outfeat_d0_stride); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) outfeat_d1_stride=%d\n", this->outfeat_d1_stride); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) outfeat_d2_stride=%d\n", this->outfeat_d2_stride); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_nb_ko=%d\n", this->subtile_nb_ko); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_rem_ko=%d\n", this->subtile_rem_ko); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_nb_ki=%d\n", this->subtile_nb_ki); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_rem_ki=%d\n", this->subtile_rem_ki); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_nb_ho=%d\n", this->subtile_nb_ho); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_rem_ho=%d\n", this->subtile_rem_ho); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_nb_wo=%d\n", this->subtile_nb_wo); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_rem_wo=%d\n", this->subtile_rem_wo); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_rem_hi=%d\n", this->subtile_rem_hi); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) subtile_rem_wi=%d\n", this->subtile_rem_wi); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) padding_top=%d\n", this->padding_top); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) padding_right=%d\n", this->padding_right); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) padding_bottom=%d\n", this->padding_bottom); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) padding_left=%d\n", this->padding_left); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) padding_value=%d\n", this->padding_value); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) Wmin=%d\n", this->Wmin); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) norm_option_shift=%s\n", this->norm_option_shift ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) norm_option_bias=%s\n", this->norm_option_bias ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) fs=%d\n", this->fs); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) output_quant=%d\n", this->output_quant); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) normalization_bits=%d\n", this->normalization_bits); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) quantization_bits=%d\n", this->quantization_bits); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) quantization_right_shift=%d\n", this->quantization_right_shift); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) use_relu=%s\n", this->use_relu ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) streamin=%s\n", this->streamin ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) filter_mask_value=%d\n", this->filter_mask_top); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) mode16=%s\n", this->mode16 ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) mode_linear=%s\n", this->mode_linear ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) strided2x2=%s\n", this->strided2x2 ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) qw=%d\n", this->qw); //int
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) depthwise=%s\n", this->depthwise ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) activation_prefetch=%s\n", this->activation_prefetch ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) weight_demux=%s\n", this->weight_demux ? "true" : "false"); //bool
  this->trace.msg(vp::Trace::LEVEL_INFO, "(cfg) signed_activation=%s\n", this->signed_activation ? "true" : "false"); //bool
  // CONVENIENCE configuration
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) h_out=%d\n", this->h_out);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) w_out=%d\n", this->w_out);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) k_out=%d\n", this->k_out);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) k_in=%d\n", this->k_in);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) h_in=%d\n", this->h_in);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) w_in=%d\n", this->w_in);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) h_out_int=%d\n", this->h_out_int);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) w_out_int=%d\n", this->w_out_int);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) h_in_int=%d\n", this->h_in_int);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(model) w_in_int=%d\n", this->w_in_int);
}

void Neureka::commit() {
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "JOB COMMITTED: job_state=%d job_pending=%d job_running=%d\n", this->job_state, this->job_pending, this->job_running);
  this->job_pending++;
  this->job_state = 0;
  this->cxt_cfg_ptr = 1-this->cxt_cfg_ptr;
}

int Neureka::acquire() {
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "JOB ACQUIRED: job_state=%d job_pending=%d job_running=%d\n", this->job_state, this->job_pending, this->job_running);
  if(this->job_state == 0 & this->job_pending < 2) {
    int job_id = (int) this->job_id++;
    this->cxt_job_id[this->cxt_cfg_ptr] = job_id;
    this->job_state = -2;
    return job_id;
  }
  else if(this->job_pending == 2) {
    return -1;
  }
  else {
    return this->job_state;
  }
}

bool Neureka::status() {
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "job_state=%d job_pending=%d\n", this->job_state, this->job_pending);
  if(this->job_state == 0 & this->job_pending == 0)
    return false;
  else
    return true;
}
