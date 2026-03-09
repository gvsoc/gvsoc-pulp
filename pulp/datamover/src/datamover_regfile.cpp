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
 * Authors: Cyrill Durrer, ETH Zurich (cdurrer@iis.ee.ethz.ch)
 */

#include <datamover.hpp>

int Datamover::hwpe_regfile_rd(int addr)
{
  int value = 0;
  switch (addr<<2)
  {
  case DATAMOVER_ACQUIRE:
    value = this->datamover_acquire;
    break;
  case DATAMOVER_FINISHED:
    value = this->datamover_finished;
    break;
  case DATAMOVER_STATUS:
    value = this->datamover_status;
    break;
  case DATAMOVER_RUNNING_JOB:
    value = this->datamover_running_job;
    break;
  default:
    this->trace.msg(vp::Trace::LEVEL_WARNING, "Read from invalid HWPE register address 0x%x\n", addr);
    break;
  }
  return value;
}

int Datamover::regfile_rd(int addr)
{
  int value = 0;
  switch (addr)
  {
  case DATAMOVER_REG_IN_PTR:
    value = this->in_ptr;
    break;
  case DATAMOVER_REG_OUT_PTR:
    value = this->out_ptr;
    break;
  case DATAMOVER_REG_TOT_LEN:
    value = this->tot_len;
    break;
  case DATAMOVER_REG_IN_D0:
    value = this->in_d0;
    break;
  case DATAMOVER_REG_IN_D1:
    value = this->in_d1;
    break;
  case DATAMOVER_REG_IN_D2:
    value = this->in_d2;
    break;
  case DATAMOVER_REG_IN_D3:
    value = this->in_d3;
    break;
  case DATAMOVER_REG_OUT_D0:
    value = this->out_d0;
    break;
  case DATAMOVER_REG_OUT_D1:
    value = this->out_d1;
    break;
  case DATAMOVER_REG_OUT_D2:
    value = this->out_d2;
    break;
  case DATAMOVER_REG_OUT_D3:
    value = this->out_d3;
    break;
  case DATAMOVER_REG_IN_OUT_D4_STRIDE:
    value = this->in_out_d4_stride;
    break;
  case DATAMOVER_REG_DIM_ENABLE:
    value = this->dim_enable;
    break;
  case DATAMOVER_REG_CTRL_ENGINE:
    value = this->ctrl_engine;
    break;
  default:
    this->trace.msg(vp::Trace::LEVEL_WARNING, "Read from invalid register address 0x%x\n", addr);
    break;
  }
  return value;
}

void Datamover::regfile_wr(int addr, int value)
{
  // printf("Datamover::regfile_wr(): Register write (addr: 0x%x, value: 0x%x)\n", addr, value);
  this->trace.msg(vp::Trace::LEVEL_TRACE, "Register write (addr: 0x%x, value: 0x%x)\n", addr, value);

  switch (addr)
  {
  case DATAMOVER_REG_IN_PTR:
    this->in_ptr = value;
    break;
  case DATAMOVER_REG_OUT_PTR:
    this->out_ptr = value;
    break;
  case DATAMOVER_REG_TOT_LEN:
    this->tot_len = value;
    break;
  case DATAMOVER_REG_IN_D0:
    this->in_d0 = value;
    break;
  case DATAMOVER_REG_IN_D1:
    this->in_d1 = value;
    break;
  case DATAMOVER_REG_IN_D2:
    this->in_d2 = value;
    break;
  case DATAMOVER_REG_IN_D3:
    this->in_d3 = value;
    break;
  case DATAMOVER_REG_OUT_D0:
    this->out_d0 = value;
    break;
  case DATAMOVER_REG_OUT_D1:
    this->out_d1 = value;
    break;
  case DATAMOVER_REG_OUT_D2:
    this->out_d2 = value;
    break;
  case DATAMOVER_REG_OUT_D3:
    this->out_d3 = value;
    break;
  case DATAMOVER_REG_IN_OUT_D4_STRIDE:
    this->in_out_d4_stride = value;
    break;
  case DATAMOVER_REG_DIM_ENABLE:
    this->dim_enable = value;
    break;
  case DATAMOVER_REG_CTRL_ENGINE:
    this->ctrl_engine = value;
    break;
  default:
    this->trace.msg(vp::Trace::LEVEL_WARNING, "Write to invalid register address 0x%x\n", addr);
    break;
  }
}

void Datamover::printout() {
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) in_ptr=%p\n", this->in_ptr);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) out_ptr=%p\n", this->out_ptr);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) tot_len=%x\n", this->tot_len);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) in_d0=%x\n", this->in_d0);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) in_d1=%x\n", this->in_d1);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) in_d2=%x\n", this->in_d2);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) in_d3=%x\n", this->in_d3);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) out_d0=%x\n", this->out_d0);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) out_d1=%x\n", this->out_d1);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) out_d2=%x\n", this->out_d2);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) out_d3=%x\n", this->out_d3);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) in_out_d4_stride=%x\n", this->in_out_d4_stride);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) dim_enable=0x%x\n", this->dim_enable);
  this->trace.msg(vp::Trace::LEVEL_DEBUG, "(cfg) ctrl_engine=0x%x\n", this->ctrl_engine);
}
