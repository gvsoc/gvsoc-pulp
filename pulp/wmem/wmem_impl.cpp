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
#include <stdio.h>
#include <math.h>

class wmem : public vp::Component
{

public:

  wmem(vp::ComponentConf &config);

  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);


private:
  vp::Trace     trace;

  vp::IoMaster **out;
  vp::IoSlave **masters_in;
  vp::IoSlave in;

  int nb_slaves;
  int nb_masters;
  uint64_t bank_mask;
  uint64_t remove_offset;
  int stage_bits;
};

wmem::wmem(vp::ComponentConf &config)
: vp::Component(config)
{
  traces.new_trace("trace", &trace, vp::DEBUG);

  in.set_req_meth(&wmem::req);
  new_slave_port("in", &in);

  nb_slaves = get_js_config()->get_child_int("nb_slaves");
  nb_masters = get_js_config()->get_child_int("nb_masters");
  stage_bits = get_js_config()->get_child_int("stage_bits");
  remove_offset = get_js_config()->get_child_int("remove_offset");


  if (stage_bits == 0)
  {
    stage_bits = log2(nb_slaves);
  }

  bank_mask = ((1<<stage_bits) - 1) & (0xFFFFFFFF);

  out = new vp::IoMaster *[nb_slaves];
  for (int i=0; i<nb_slaves; i++)
  {
    out[i] = new vp::IoMaster();
    new_master_port("out_" + std::to_string(i), out[i]);
  }

  masters_in = new vp::IoSlave *[nb_masters];

  for (int i=0; i<nb_masters; i++)
  {
    masters_in[i] = new vp::IoSlave();
    masters_in[i]->set_req_meth(&wmem::req);
    new_slave_port("in_" + std::to_string(i), masters_in[i]);
  }


}

vp::IoReqStatus wmem::req(vp::Block *__this, vp::IoReq *req)
{
  wmem *_this = (wmem *)__this;
  bool is_write = req->get_is_write();
  uint64_t MASK = 0x0FFFFFFFF;
  uint64_t remove_offset_mask = _this->remove_offset & MASK;
  uint64_t size = req->get_size();
  uint8_t *data = req->get_data();
  uint64_t addr = req->get_addr()-remove_offset_mask;


  // _this->trace.msg("Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n", offset, size, is_write);
  _this->trace.msg("Received IO req (addr: 0x%llx, size: 0x%llx, is_write: %d, bank_mask: %d, stage_bits: %d, remove_offste:0x%llx)\n", addr, size, is_write, _this->bank_mask, _this->stage_bits, _this->remove_offset);

 
  int bank_id = (addr >> 2) & _this->bank_mask;
  uint64_t bank_offset = ((addr >> (_this->stage_bits + 2)) << 2) + (addr & 0x3);

  req->set_addr(bank_offset);
  return _this->out[bank_id]->req_forward(req);
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new wmem(config);
}
