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
#include <string.h>

class icache_ctrl : public vp::Component
{

public:

  icache_ctrl(vp::ComponentConf &config);

  static vp::IoReqStatus req(void *__this, vp::IoReq *req);

private:

  vp::Trace     trace;
  vp::IoSlave in;

  vp::WireMaster<bool>     enable_itf;
  vp::WireMaster<bool>     flush_itf;
  vp::WireMaster<bool>     flush_line_itf;
  vp::WireMaster<uint32_t> flush_line_addr_itf;
};

icache_ctrl::icache_ctrl(vp::ComponentConf &config)
: vp::Component(config)
{
  traces.new_trace("trace", &trace, vp::DEBUG);
  in.set_req_meth(&icache_ctrl::req);
  new_slave_port("input", &in);

  this->new_master_port("enable", &this->enable_itf);
  this->new_master_port("flush", &this->flush_itf);
  this->new_master_port("flush_line", &this->flush_line_itf);
  this->new_master_port("flush_line_addr", &this->flush_line_addr_itf);


}

vp::IoReqStatus icache_ctrl::req(void *__this, vp::IoReq *req)
{
  icache_ctrl *_this = (icache_ctrl *)__this;

  uint64_t offset = req->get_addr();
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();
  bool is_write = req->get_is_write();

  _this->trace.msg("icache_ctrl access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

  if (offset == 0)
  {
    if (_this->enable_itf.is_bound())
      _this->enable_itf.sync(*data != 0);
  }
  else if (offset == 4)
  {
    _this->trace.msg("Flushing cache\n");
    _this->flush_itf.sync(true);
  }

  return vp::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new icache_ctrl(config);
}
