/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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
 * Authors: Germain Haugou, GreenWaves Technologies (germain.haugou@greenwaves-technologies.com)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <string.h>

#define CHIMERA_SNITCH_BOOT_ADDR_OFFSET         0x0
#define CHIMERA_SNITCH_INTR_HANDLER_ADDR_OFFSET 0x4
#define CHIMERA_SNITCH_CLUSTER_0_RETURN_OFFSET  0x8
#define CHIMERA_SNITCH_CLUSTER_1_RETURN_OFFSET  0xc
#define CHIMERA_SNITCH_CLUSTER_2_RETURN_OFFSET  0x10
#define CHIMERA_SNITCH_CLUSTER_3_RETURN_OFFSET  0x14
#define CHIMERA_SNITCH_CLUSTER_4_RETURN_OFFSET  0x18
#define CHIMERA_CLUSTER_0_CLK_GATE_EN_OFFSET    0x1c
#define CHIMERA_CLUSTER_1_CLK_GATE_EN_OFFSET    0x20
#define CHIMERA_CLUSTER_2_CLK_GATE_EN_OFFSET    0x24
#define CHIMERA_CLUSTER_3_CLK_GATE_EN_OFFSET    0x28
#define CHIMERA_CLUSTER_4_CLK_GATE_EN_OFFSET    0x2c
#define CHIMERA_WIDE_MEM_CLUSTER_0_BYPASS_OFFSET  0x30
#define CHIMERA_WIDE_MEM_CLUSTER_1_BYPASS_OFFSET  0x34
#define CHIMERA_WIDE_MEM_CLUSTER_2_BYPASS_OFFSET  0x38
#define CHIMERA_WIDE_MEM_CLUSTER_3_BYPASS_OFFSET  0x3c
#define CHIMERA_WIDE_MEM_CLUSTER_4_BYPASS_OFFSET  0x40
#define CHIMERA_CLUSTER_0_BUSY_OFFSET  0x44
#define CHIMERA_CLUSTER_1_BUSY_OFFSET  0x48
#define CHIMERA_CLUSTER_2_BUSY_OFFSET  0x4c
#define CHIMERA_CLUSTER_3_BUSY_OFFSET  0x50
#define CHIMERA_CLUSTER_4_BUSY_OFFSET  0x54

#define CHIMERA_NUM_REGS 22

class chimera_reg_top : public vp::Component
{

public:

  chimera_reg_top(vp::ComponentConf &config);

  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:

  void reset(bool active);
  static void bootaddr_sync(vp::Block *__this, uint32_t value);

  vp::Trace     trace;
  vp::IoSlave in;

  uint32_t registers[CHIMERA_NUM_REGS];

  vp::WireMaster<uint32_t> bootaddr_itf;

  vp::WireMaster<uint32_t> confreg_soc_itf;
  vp::WireSlave<uint32_t> confreg_ext_itf;
};

chimera_reg_top::chimera_reg_top(vp::ComponentConf &config)
: vp::Component(config)
{
  traces.new_trace("trace", &trace, vp::DEBUG);
  in.set_req_meth(&chimera_reg_top::req);
  new_slave_port("input", &in);

  bootaddr_itf.set_sync_meth(&chimera_reg_top::bootaddr_sync);
  new_master_port("bootaddr", &this->bootaddr_itf);
}

vp::IoReqStatus chimera_reg_top::req(vp::Block *__this, vp::IoReq *req)
{
  chimera_reg_top *_this = (chimera_reg_top *)__this;

  uint64_t offset = req->get_addr()>>2;
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();
  bool is_write = req->get_is_write();
  if(is_write){
    _this->registers[offset] = *((uint32_t*) data);
  } else {
    *data = (uint8_t)(*(_this->registers + offset));
  }
  _this->trace.msg("offset: 0x%x, data: 0x%x, write: %d\n", offset, *data, is_write );

  return vp::IO_REQ_OK;
}

void chimera_reg_top::bootaddr_sync(vp::Block *__this, uint32_t value)
{
  chimera_reg_top *_this = (chimera_reg_top *)__this;
  printf("SET ENTRY %llx\n", value);
//   _this->bootaddr = value;
}

void chimera_reg_top::reset(bool active)
{
    for(int i=0; i<CHIMERA_NUM_REGS; i++){
        registers[i] = 0x0;
    }
    registers[CHIMERA_CLUSTER_0_BUSY_OFFSET>>2] = 0x1;
    registers[CHIMERA_CLUSTER_1_BUSY_OFFSET>>2] = 0x1;
    registers[CHIMERA_CLUSTER_2_BUSY_OFFSET>>2] = 0x1;
    registers[CHIMERA_CLUSTER_3_BUSY_OFFSET>>2] = 0x1;
    registers[CHIMERA_CLUSTER_4_BUSY_OFFSET>>2] = 0x1;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new chimera_reg_top(config);
}
