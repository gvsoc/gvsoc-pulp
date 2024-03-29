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
#include <vector>

#include "archi/gpio_v3.h"

class Gpio : public vp::Component
{

public:

  Gpio(vp::ComponentConf &config);

private:

  static void gpio_sync(vp::Block *__this, bool value, int gpio);
  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

  vp::IoReqStatus paddir_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus padin_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus padout_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus inten_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus inttype0_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus inttype1_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus intstatus_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus gpioen_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus padcfg_req(int id, int reg_offset, int size, bool is_write, uint8_t *data);

  vp::Trace trace;

  vp::IoSlave in;

  std::vector<vp::WireSlave<bool> *> gpio_itf;
  vp::WireMaster<int>  event_itf;
  vp::WireMaster<bool> irq_itf;

  int nb_gpio;
  int soc_event;

  vp_gpio_paddir               r_paddir;
  vp_gpio_padin                r_padin;
  vp_gpio_padout               r_padout;
  vp_gpio_inten                r_inten;
  vp_gpio_inttype0             r_inttype0;
  vp_gpio_inttype1             r_inttype1;
  vp_gpio_intstatus            r_intstatus;
  vp_gpio_gpioen               r_gpioen;
  std::vector<vp_gpio_padcfg0> r_padcfg;
};



Gpio::Gpio(vp::ComponentConf &config)
: vp::Component(config)
{
  this->traces.new_trace("trace", &this->trace, vp::DEBUG);

  this->in.set_req_meth(&Gpio::req);
  this->new_slave_port("input", &this->in);
  this->new_master_port("event", &this->event_itf);
  this->new_master_port("irq", &this->irq_itf);

  this->soc_event = this->get_js_config()->get_child_int("soc_event");
  this->nb_gpio = this->get_js_config()->get_int("nb_gpio");

  for (int i=0; i<this->nb_gpio; i++)
  {
    vp::WireSlave<bool> *itf = new vp::WireSlave<bool>();
    itf->set_sync_meth_muxed(&Gpio::gpio_sync, i);
    new_slave_port("gpio" + std::to_string(i), itf);
    this->gpio_itf.push_back(itf);
  }

  this->new_reg("paddir", &this->r_paddir, 0);
  this->new_reg("padin", &this->r_padin, 0);
  this->new_reg("padout", &this->r_padout, 0);
  this->new_reg("inten", &this->r_inten, 0);
  this->new_reg("inttype0", &this->r_inttype0, 0);
  this->new_reg("inttype1", &this->r_inttype1, 0);
  this->new_reg("intstatus", &this->r_intstatus, 0);
  this->new_reg("gpioen", &this->r_gpioen, 0);

  this->r_padcfg.resize(this->nb_gpio/4);

  for (int i=0; i<this->nb_gpio/4; i++)
  {
    this->new_reg("padcfg" + std::to_string(i), &this->r_padcfg[i], 0);
  }


}





vp::IoReqStatus Gpio::paddir_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
  this->r_paddir.access(reg_offset, size, data, is_write);
  return vp::IO_REQ_OK;
}

vp::IoReqStatus Gpio::padin_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
  this->r_padin.access(reg_offset, size, data, is_write);
  return vp::IO_REQ_OK;
}

vp::IoReqStatus Gpio::padout_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
  // TODO GPIO output should be propagated to pads. This should take gpioen into account only on some architecture
  uint32_t old_val = this->r_padout.get();

  this->r_padout.access(reg_offset, size, data, is_write);

  uint32_t new_val = this->r_padout.get();

  uint32_t changed = old_val ^ new_val;

  for (int i=0; i<this->nb_gpio; i++)
  {
    if ((changed >> i) & 1)
    {
      if (this->gpio_itf[i]->is_bound())
      {
        this->gpio_itf[i]->sync((new_val >> i) & 1);
      }
    }
  }


  return vp::IO_REQ_OK;
}

vp::IoReqStatus Gpio::inten_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
  this->r_inten.access(reg_offset, size, data, is_write);
  return vp::IO_REQ_OK;
}

vp::IoReqStatus Gpio::inttype0_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
  this->r_inttype0.access(reg_offset, size, data, is_write);
  return vp::IO_REQ_OK;
}

vp::IoReqStatus Gpio::inttype1_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
  this->r_inttype1.access(reg_offset, size, data, is_write);
  return vp::IO_REQ_OK;
}

vp::IoReqStatus Gpio::intstatus_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
  this->r_intstatus.access(reg_offset, size, data, is_write);
  return vp::IO_REQ_OK;
}

vp::IoReqStatus Gpio::gpioen_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
  this->r_gpioen.access(reg_offset, size, data, is_write);
  return vp::IO_REQ_OK;
}

vp::IoReqStatus Gpio::padcfg_req(int id, int reg_offset, int size, bool is_write, uint8_t *data)
{
  this->r_padcfg[id].access(reg_offset, size, data, is_write);
  return vp::IO_REQ_OK;
}



vp::IoReqStatus Gpio::req(vp::Block *__this, vp::IoReq *req)
{
  Gpio *_this = (Gpio *)__this;

  vp::IoReqStatus err = vp::IO_REQ_INVALID;

  uint64_t offset = req->get_addr();
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();
  uint64_t is_write = req->get_is_write();

  _this->trace.msg("GPIO access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, req->get_is_write());

  int reg_id = offset / 4;
  int reg_offset = offset % 4;

  if (reg_offset + size > 4) {
    _this->trace.force_warning("Accessing 2 registers in one access\n");
    goto error;
  }

  switch (reg_id)
  {
    case GPIO_PADDIR_OFFSET/4:
      err = _this->paddir_req(reg_offset, size, is_write, data);
      break;

    case GPIO_PADIN_OFFSET/4:
      err = _this->padin_req(reg_offset, size, is_write, data);
      break;

    case GPIO_PADOUT_OFFSET/4:
      err = _this->padout_req(reg_offset, size, is_write, data);
      break;

    case GPIO_INTEN_OFFSET/4:
      err = _this->inten_req(reg_offset, size, is_write, data);
      break;

    case GPIO_INTTYPE0_OFFSET/4:
      err = _this->inttype0_req(reg_offset, size, is_write, data);
      break;

    case GPIO_INTTYPE1_OFFSET/4:
      err = _this->inttype1_req(reg_offset, size, is_write, data);
      break;

    case GPIO_INTSTATUS_OFFSET/4:
      err = _this->intstatus_req(reg_offset, size, is_write, data);
      break;

    case GPIO_GPIOEN_OFFSET/4:
      err = _this->gpioen_req(reg_offset, size, is_write, data);
      break;

  }

  if (reg_id >= GPIO_PADCFG0_OFFSET/4 && reg_id <= GPIO_PADCFG3_OFFSET/4)
    err = _this->padcfg_req(reg_id - GPIO_PADCFG0_OFFSET/4, reg_offset, size, is_write, data);

  if (err != vp::IO_REQ_OK)
    goto error; 


  return vp::IO_REQ_OK;

error:
  _this->trace.force_warning("GPIO invalid access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

  return vp::IO_REQ_INVALID;
}



void Gpio::gpio_sync(vp::Block *__this, bool value, int gpio)
{
  Gpio *_this = (Gpio *)__this;

  _this->trace.msg("Received new gpio value (gpio: %d, value: %d)\n", gpio, value);

  unsigned int old_val = (_this->r_padin.get() >> gpio) & 1;
  _this->r_padin.set((_this->r_padin.get() & ~(1<<gpio)) | (value << gpio));

  if (((_this->r_inten.get() >> gpio) & 1) && old_val != value)
  {
    // The inttype is coded with 2 bits per gpio
    // Extract it for our gpio in the proper register
    int reg_id = gpio  / 16;
    int bit = gpio % 16;
    uint32_t inttype_reg = reg_id ? _this->r_inttype1.get() : _this->r_inttype0.get();
    int inttype = (inttype_reg >> (2*bit)) & 0x3;

    // The interrupt should be raised if the edge is matching the mode
    // (0: falling, 1: rising, 2: both)
    int edge = (old_val == 0 && (inttype == 1 || inttype == 2)) ||
      (old_val == 1 && (inttype == 0 || inttype == 2));

    if (edge)
    {
      _this->r_intstatus.set(_this->r_intstatus.get() | (1 << gpio));

      _this->trace.msg("Raising interrupt (intstatus: 0x%x)\n", _this->r_intstatus.get());

      if (_this->event_itf.is_bound())
        _this->event_itf.sync(_this->soc_event);

      if (_this->irq_itf.is_bound())
        _this->irq_itf.sync(true);
    }
  }
}




extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new Gpio(config);
}
