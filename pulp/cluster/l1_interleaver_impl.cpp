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
#include <vp/debug_mem.hpp>
#include <stdio.h>
#include <math.h>
#include <vector>

class interleaver : public vp::Component, public vp::DebugMemIf
{

public:

  interleaver(vp::ComponentConf &config);

  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
  static vp::IoReqStatus req_ts(vp::Block *__this, vp::IoReq *req);

  // Backdoor debug access (vp/debug_mem.hpp). Like the io_v2 log_ico, a flat
  // region cannot express the bank interleaving, so the interleaver keeps the
  // default debug_mem_regions() (advertising itself as one terminal region)
  // and redoes the bank math per interleaving granule in debug_mem_access().
  vp::DebugMemIf *debug_mem_if() override { return this; }
  int debug_mem_access(uint64_t addr, uint8_t *data, uint64_t size,
    bool is_write) override;


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
  uint64_t offset_mask;
  std::vector<vp::DebugMemIf *> bank_debug_mem;
  bool bank_debug_mem_resolved = false;
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
  offset_mask = get_js_config()->get_child_int("offset_mask");
  interleaving_bits = get_js_config()->get_child_int("interleaving_bits");

  if (stage_bits == 0)
  {
    stage_bits = log2(nb_slaves);
  }

  bank_mask = (1<<stage_bits) - 1;

  if (offset_mask == 0)
  {
      offset_mask = -1;
  }

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

int interleaver::debug_mem_access(uint64_t addr, uint8_t *data, uint64_t size,
  bool is_write)
{
  if (!this->bank_debug_mem_resolved)
  {
    this->bank_debug_mem_resolved = true;
    this->bank_debug_mem.assign(this->nb_slaves, nullptr);
    for (int i = 0; i < this->nb_slaves; i++)
    {
      std::vector<vp::SlavePort *> finals = this->out[i]->get_final_ports();
      if (!finals.empty() && finals[0]->get_owner() != nullptr)
      {
        this->bank_debug_mem[i] = finals[0]->get_owner()->debug_mem_if();
      }
    }
  }

  // Walk the access one interleaving granule at a time, each chunk going to
  // its bank at the bank-local offset (same math as the timed req() path).
  uint64_t granule = 1ULL << this->interleaving_bits;
  while (size > 0)
  {
    int bank_id = (addr >> this->interleaving_bits) & this->bank_mask;
    uint64_t bank_offset =
      ((addr >> (this->stage_bits + this->interleaving_bits)) << this->interleaving_bits)
      + (addr & (granule - 1));

    uint64_t chunk = granule - (addr & (granule - 1));
    if (chunk > size)
    {
      chunk = size;
    }

    if (this->bank_debug_mem[bank_id] == nullptr ||
        this->bank_debug_mem[bank_id]->debug_mem_access(bank_offset, data, chunk, is_write))
    {
      return -1;
    }

    addr += chunk;
    data += chunk;
    size -= chunk;
  }

  return 0;
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
  offset &= _this->offset_mask;
  uint64_t bank_offset = ((offset >> (_this->stage_bits + _this->interleaving_bits)) << _this->interleaving_bits) + (offset & ((1<<_this->interleaving_bits)-1));

  _this->trace.msg("Forwarding interleaved packet (port: %d, offset: 0x%x, size: 0x%x)\n", bank_id, bank_offset, size);

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
