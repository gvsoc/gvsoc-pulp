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

#include "archi/apb_soc.h"

class apb_soc_ctrl : public vp::Component
{

public:

  apb_soc_ctrl(vp::ComponentConf &config);

  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:

  void reset(bool active);
  static void bootsel_sync(vp::Block *__this, int value);
  static void confreg_ext_sync(vp::Block *__this, uint32_t value);
  static void bootaddr_sync(vp::Block *__this, uint64_t value);

  vp::Trace     trace;
  vp::IoSlave in;

  vp::WireMaster<uint32_t> bootaddr_itf;
  vp::WireMaster<int>  event_itf;
  vp::WireSlave<int>   bootsel_itf;

  vp::WireMaster<uint32_t> confreg_soc_itf;
  vp::WireSlave<uint32_t> confreg_ext_itf;

  vp::WireSlave<uint64_t> rom_bootaddr_itf;

  uint32_t core_status;
  uint32_t bootaddr;
  int bootsel;

  vp::reg_32     jtag_reg_ext;
};

apb_soc_ctrl::apb_soc_ctrl(vp::ComponentConf &config)
: vp::Component(config)
{
  traces.new_trace("trace", &trace, vp::DEBUG);
  in.set_req_meth(&apb_soc_ctrl::req);
  new_slave_port("input", &in);

  bootsel_itf.set_sync_meth(&apb_soc_ctrl::bootsel_sync);
  new_slave_port("bootsel", &bootsel_itf);

  new_master_port("bootaddr", &this->bootaddr_itf);

  new_master_port("event", &event_itf);

  confreg_ext_itf.set_sync_meth(&apb_soc_ctrl::confreg_ext_sync);
  this->new_slave_port("confreg_ext", &this->confreg_ext_itf);

  rom_bootaddr_itf.set_sync_meth(&apb_soc_ctrl::bootaddr_sync);
    this->new_slave_port("rom_bootaddr", &this->rom_bootaddr_itf);

  this->new_master_port("confreg_soc", &this->confreg_soc_itf);

  this->new_reg("jtag_reg_ext", &this->jtag_reg_ext, 0, false);

  core_status = 0;
  this->bootsel = 0;
  this->jtag_reg_ext.set(0);


}

vp::IoReqStatus apb_soc_ctrl::req(vp::Block *__this, vp::IoReq *req)
{
  apb_soc_ctrl *_this = (apb_soc_ctrl *)__this;

  uint64_t offset = req->get_addr();
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();
  bool is_write = req->get_is_write();

  _this->trace.msg("Apb_soc_ctrl access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

  if (size != 4) return vp::IO_REQ_INVALID;

  if (offset == APB_SOC_CORESTATUS_OFFSET)
  {
    if (!is_write)
    {
      *(uint32_t *)data = _this->core_status;
    }
    else
    {
      // We are writing to the status register, the 31 LSBs are the return value of the platform and the last bit
      // makes the platform exit when set to 1
      _this->core_status = *(uint32_t *)data;

      if ((_this->core_status >> APB_SOC_STATUS_EOC_BIT) & 1)
      {
        _this->time.get_engine()->quit(_this->core_status & 0x7fffffff);
      }
    }
  }
  else if (offset == APB_SOC_PADS_CONFIG)
  {
    if (!is_write)
    {
      *(uint32_t *)data = _this->bootsel;
    }
  }
  else if (offset == APB_SOC_BOOTADDR_OFFSET || offset == 0x48)
  {
    if (is_write)
    {
      _this->trace.msg("Setting boot address (addr: 0x%x)\n", *(uint32_t *)data);
      if (_this->bootaddr_itf.is_bound())
        _this->bootaddr_itf.sync(*(uint32_t *)data);

      _this->bootaddr = *(uint32_t *)data;
    }
    else *(uint32_t *)data = _this->bootaddr;
  }
  else if (offset == APB_SOC_JTAG_REG)
  {
    if (is_write)
    {
      _this->confreg_soc_itf.sync(*(uint32_t *)data);
    }
    else
    {
      *(uint32_t *)data = _this->jtag_reg_ext.get() << APB_SOC_JTAG_REG_EXT_BIT;
    }
  }
  else if (offset == 0x4c)
  {
      if (!is_write)
      {
          *(uint32_t *)data = 1;
      }
  }
  else if (offset == 0x8)
  {
      if (is_write)
      {
          uint32_t value = *(uint32_t *)data;
          if (value & 1)
          {
              _this->time.get_engine()->quit(value >> 1);
          }
      }
  }
  else
  {
      printf("UNRESOLVED %llx\n", offset);
  }


  return vp::IO_REQ_OK;
}

void apb_soc_ctrl::bootsel_sync(vp::Block *__this, int value)
{
  apb_soc_ctrl *_this = (apb_soc_ctrl *)__this;
  _this->bootsel = value;
}

void apb_soc_ctrl::confreg_ext_sync(vp::Block *__this, uint32_t value)
{
  apb_soc_ctrl *_this = (apb_soc_ctrl *)__this;
  _this->jtag_reg_ext.set(value);
}

void apb_soc_ctrl::bootaddr_sync(vp::Block *__this, uint64_t value)
{
  apb_soc_ctrl *_this = (apb_soc_ctrl *)__this;
  printf("SET ENTRY %llx\n", value);
  _this->bootaddr = value;
}

void apb_soc_ctrl::reset(bool active)
{
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new apb_soc_ctrl(config);
}
