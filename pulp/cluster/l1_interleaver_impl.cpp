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

class interleaver : public vp::Component
{

public:

  interleaver(vp::ComponentConf &config);

  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
  static vp::IoReqStatus req_ts(vp::Block *__this, vp::IoReq *req);


private:
  vp::Trace     trace;

  vp::IoMaster **out;
  vp::IoSlave **masters_in;
  vp::IoSlave **masters_ts_in;
  vp::IoSlave in;
  vp::IoSlave ts_in;

  int nb_slaves;
  int nb_masters;
  int stage_bits;
  uint64_t bank_mask;
  vp::IoReq ts_req;
  int interleaving_bits;
};

interleaver::interleaver(vp::ComponentConf &config)
: vp::Component(config)
{
  traces.new_trace("trace", &trace, vp::DEBUG);

  in.set_req_meth(&interleaver::req);
  new_slave_port("in", &in);

  nb_slaves = get_js_config()->get_child_int("nb_slaves");
  nb_masters = get_js_config()->get_child_int("nb_masters");
  stage_bits = get_js_config()->get_child_int("stage_bits");
  interleaving_bits = get_js_config()->get_child_int("interleaving_bits");

  if (stage_bits == 0)
  {
    stage_bits = log2(nb_slaves);
  }

  bank_mask = (1<<stage_bits) - 1;

  out = new vp::IoMaster *[nb_slaves];
  for (int i=0; i<nb_slaves; i++)
  {
    out[i] = new vp::IoMaster();
    new_master_port("out_" + std::to_string(i), out[i]);
  }

  masters_in = new vp::IoSlave *[nb_masters];
  masters_ts_in = new vp::IoSlave *[nb_masters];
  for (int i=0; i<nb_masters; i++)
  {
    masters_in[i] = new vp::IoSlave();
    masters_in[i]->set_req_meth(&interleaver::req);
    new_slave_port("in_" + std::to_string(i), masters_in[i]);

    masters_ts_in[i] = new vp::IoSlave();
    masters_ts_in[i]->set_req_meth(&interleaver::req_ts);
    new_slave_port("ts_in_" + std::to_string(i), masters_ts_in[i]);
  }


}

vp::IoReqStatus interleaver::req(vp::Block *__this, vp::IoReq *req)
{
  interleaver *_this = (interleaver *)__this;
  uint64_t offset = req->get_addr();
  bool is_write = req->get_is_write();
  uint64_t size = req->get_size();
  uint8_t *data = req->get_data();

  _this->trace.msg("Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n", offset, size, is_write);
 
  int bank_id = (offset >> _this->interleaving_bits) & _this->bank_mask;
  uint64_t bank_offset = ((offset >> (_this->stage_bits + _this->interleaving_bits)) << _this->interleaving_bits) + (offset & ((1<<_this->interleaving_bits)-1));

  req->set_addr(bank_offset);
  return _this->out[bank_id]->req_forward(req);
}

vp::IoReqStatus interleaver::req_ts(vp::Block *__this, vp::IoReq *req)
{
  interleaver *_this = (interleaver *)__this;
  uint64_t offset = req->get_addr();
  bool is_write = req->get_is_write();
  uint64_t size = req->get_size();
  uint8_t *data = req->get_data();

  _this->trace.msg("Received TS IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n", offset, size, is_write);
 
  int bank_id = (offset >> _this->interleaving_bits) & _this->bank_mask;
  uint64_t bank_offset = ((offset >> (_this->stage_bits + 2)) << 2) + (offset & 0x3);

  bank_offset &= ~(1<<(20 - _this->stage_bits));

  if (!is_write)
  {
    req->set_addr(bank_offset);
    vp::IoReqStatus err = _this->out[bank_id]->req_forward(req);
    if (err != vp::IO_REQ_OK) return err;
    _this->trace.msg("Sending test-and-set IO req (offset: 0x%llx, size: 0x%llx)\n", offset & ~(1<<20), size);
    uint64_t ts_data = -1;
    _this->ts_req.set_addr(bank_offset);
    _this->ts_req.set_size(size);
    _this->ts_req.set_is_write(true);
    _this->ts_req.set_data((uint8_t *)&ts_data);
    return _this->out[bank_id]->req(&_this->ts_req);
  }

  req->set_addr(bank_offset);
  return _this->out[bank_id]->req_forward(req);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new interleaver(config);
}


