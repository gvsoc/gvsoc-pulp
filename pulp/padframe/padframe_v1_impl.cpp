/*
 * Copyright (C) 2020 ETH Zurich
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
 * Authors: Germain Haugou, ETH Zurich (germain.haugou@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/qspim.hpp>
#include <vp/itf/uart.hpp>
#include <vp/itf/jtag.hpp>
#include <vp/itf/cpi.hpp>
#include <vp/itf/hyper.hpp>
#include <vp/itf/clock.hpp>
#include <vp/itf/i2c.hpp>
#include <vp/itf/i2s.hpp>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace std;

class Pad_group
{
public:
  Pad_group(std::string name) : name(name) {}

  std::string name;
};

class Qspim_group : public Pad_group
{
public:
  Qspim_group(std::string name) : Pad_group(name) {}
  vp::QspimSlave slave;
  vp::Trace data_0_trace;
  vp::Trace data_1_trace;
  vp::Trace data_2_trace;
  vp::Trace data_3_trace;
  int nb_cs;
  vector<vp::Trace *> cs_trace;
  vector<vp::QspimMaster *> master;
  vector<vp::WireMaster<bool> *> cs_master;
  int active_cs;
};

class Cpi_group : public Pad_group
{
public:
  Cpi_group(std::string name) : Pad_group(name) {}
  vp::CpiSlave slave;
  vp::CpiMaster master;
  vp::Trace pclk_trace;
  vp::Trace href_trace;
  vp::Trace vsync_trace;
  vp::Trace data_trace;
};

class Jtag_group : public Pad_group
{
public:
  Jtag_group(std::string name) : Pad_group(name) {}
  vp::JtagSlave pad_slave;
  vp::JtagSlave chip_slave;
  vp::JtagMaster master;
  vp::Trace tck_trace;
  vp::Trace tdi_trace;
  vp::Trace tms_trace;
  vp::Trace trst_trace;
  vp::Trace tdo_trace;
};

class Uart_group : public Pad_group
{
public:
  Uart_group(std::string name) : Pad_group(name) {}
  vp::UartSlave slave;
  vp::UartMaster master;
  vp::Trace tx_trace;
  vp::Trace rx_trace;
};

class I2s_group : public Pad_group
{
public:
  I2s_group(std::string name) : Pad_group(name) {}
  vp::I2sSlave slave;
  vp::I2sMaster master;
  vp::Trace sck_trace;
  vp::Trace ws_trace;
  vp::Trace sdi_trace;
  vp::Trace sdo_trace;
  int sck_in;
  int ws_in;
  int sdi_in;
  int sdo_in;
  int sck_out;
  int ws_out;
  int sdi_out;
  int sdo_out;
};

class I2c_group : public Pad_group
{
public:
  I2c_group(std::string name) : Pad_group(name) {}
  vp::I2cSlave slave;
  vp::I2cMaster master;
  vp::Trace scl_trace;
  vp::Trace sda_trace;
};

class Hyper_group : public Pad_group
{
public:
  Hyper_group(std::string name) : Pad_group(name) {}
  vp::HyperSlave slave;
  vp::Trace data_trace;
  int nb_cs;
  vector<vp::Trace *> cs_trace;
  vector<vp::HyperMaster *> master;
  vector<vp::WireMaster<bool> *> cs_master;
  int active_cs;
};

class Wire_group : public Pad_group
{
public:
  Wire_group(std::string name) : Pad_group(name) {}
  vp::WireSlave<int> slave;
  vp::WireMaster<int> master;
};

class Gpio_group : public Pad_group
{
public:
  Gpio_group(std::string name) : Pad_group(name) {}
  vp::WireMaster<int> master;
};

class padframe : public vp::Component
{

public:

  padframe(vp::ComponentConf &config);

  static vp::IoReqStatus req(void *__this, vp::IoReq *req);

private:

  static void qspim_master_sync(void *__this, int sck, int data_0, int data_1, int data_2, int data_3, int mask, int id);
  static void qspim_sync(void *__this, int sck, int data_0, int data_1, int data_2, int data_3, int mask, int id);
  static void qspim_cs_sync(void *__this, int cs, int active, int id);

  static void jtag_pad_slave_sync(void *__this, int tck, int tdi, int tms, int trst, int id);
  static void jtag_pad_slave_sync_cycle(void *__this, int tdi, int tms, int trst, int id);

  static void jtag_chip_slave_sync(void *__this, int tck, int tdi, int tms, int trst, int id);
  static void jtag_chip_slave_sync_cycle(void *__this, int tdi, int tms, int trst, int id);
  
  static void jtag_master_sync(void *__this, int tdo, int id);

  static void cpi_sync(void *__this, int pclk, int href, int vsync, int data, int id);
  static void cpi_sync_cycle(void *__this, int href, int vsync, int data, int id);

  static void uart_chip_sync(void *__this, int data, int id);
  static void uart_master_sync(void *__this, int data, int id);

  static void i2s_internal_edge(void *__this, int sck, int ws, int sd, bool full_duplex, int id);
  static void i2s_external_edge(void *__this, int sck, int ws, int sd, bool full_duplex, int id);

  static void i2c_chip_sync(void *__this, int scl, int sda, int id);
  static void i2c_master_sync(void *__this, int scl, int data, int id);

  static void hyper_master_sync_cycle(void *__this, int data, int id);
  static void hyper_sync_cycle(void *__this, int data, int id);
  static void hyper_cs_sync(void *__this, int cs, int active, int id);

  static void master_wire_sync(void *__this, int value, int id);
  static void wire_sync(void *__this, int value, int id);

  static void master_gpio_sync(void *__this, int value, int id);
  static void gpio_sync(void *__this, int value, int id);

  static void ref_clock_sync(void *__this, bool value);
  static void ref_clock_set_frequency(void *, int64_t value);

  void set_pad(int *padin_value, int *padout_value, int *pad_value);

  vp::Trace     trace;
  vp::IoSlave in;

  vector<Pad_group *> groups;
  vp::ClockSlave    ref_clock_pad_itf;
  vp::ClockMaster    ref_clock_itf;

  vp::Trace ref_clock_trace;

  int nb_itf = 0;
};

padframe::padframe(vp::ComponentConf &config)
: vp::Component(config)
{
  traces.new_trace("trace", &trace, vp::DEBUG);
  in.set_req_meth(&padframe::req);

  new_slave_port("in", &in);

  ref_clock_pad_itf.set_sync_meth(&padframe::ref_clock_sync);
  ref_clock_pad_itf.set_set_frequency_meth(&padframe::ref_clock_set_frequency);
  new_slave_port("ref_clock_pad", &this->ref_clock_pad_itf);

  new_master_port("ref_clock", &this->ref_clock_itf);

  this->traces.new_trace_event("ref_clock", &this->ref_clock_trace, 1);

  js::Config *groups = get_js_config()->get("groups");

  for (auto& group: groups->get_childs())
  {
    std::string name = group.first;
    js::Config *config = group.second;
    js::Config *type_config = config->get("type");
    if (type_config)
    {
      std::string type = type_config->get_str();

      trace.msg("Found pad group (group: %s, type: %s)\n",
        name.c_str(), type.c_str());

      if (type == "qspim")
      {
        Qspim_group *group = new Qspim_group(name);
        new_slave_port(name, &group->slave);
        group->active_cs = -1;
        group->slave.set_sync_meth_muxed(&padframe::qspim_sync, nb_itf);
        group->slave.set_cs_sync_meth_muxed(&padframe::qspim_cs_sync, nb_itf);
        this->groups.push_back(group);

        traces.new_trace_event(name + "/data_0", &group->data_0_trace, 1);
        traces.new_trace_event(name + "/data_1", &group->data_1_trace, 1);
        traces.new_trace_event(name + "/data_2", &group->data_2_trace, 1);
        traces.new_trace_event(name + "/data_3", &group->data_3_trace, 1);
        js::Config *nb_cs_config = config->get("nb_cs");
        group->nb_cs = nb_cs_config ? nb_cs_config->get_int() : 1;
        for (int i=0; i<group->nb_cs; i++)
        {
          vp::Trace *trace = new vp::Trace;
          traces.new_trace_event(name + "/cs_" + std::to_string(i), trace, 4);
          group->cs_trace.push_back(trace);
          vp::QspimMaster *itf = new vp::QspimMaster;
          itf->set_sync_meth_muxed(&padframe::qspim_master_sync, nb_itf);

          new_master_port(name + "_cs" + std::to_string(i) + "_data_pad", itf);
          group->master.push_back(itf);

          vp::WireMaster<bool> *cs_itf = new vp::WireMaster<bool>;
          new_master_port(name + "_cs" + std::to_string(i) + "_pad", cs_itf);
          group->cs_master.push_back(cs_itf);
        }

        nb_itf++;
      }
      else if (type == "jtag")
      {
        Jtag_group *group = new Jtag_group(name);
        new_master_port(name, &group->master);
        new_slave_port(name + "_pad", &group->pad_slave);
        new_slave_port(name + "_out", &group->chip_slave);

        group->pad_slave.set_sync_meth_muxed(&padframe::jtag_pad_slave_sync, nb_itf);
        group->pad_slave.set_sync_cycle_meth_muxed(&padframe::jtag_pad_slave_sync_cycle, nb_itf);

        group->chip_slave.set_sync_meth_muxed(&padframe::jtag_chip_slave_sync, nb_itf);
        group->chip_slave.set_sync_cycle_meth_muxed(&padframe::jtag_chip_slave_sync_cycle, nb_itf);

        this->groups.push_back(group);

        traces.new_trace_event(name + "/tck", &group->tck_trace, 1);
        traces.new_trace_event(name + "/tdi", &group->tdi_trace, 1);
        traces.new_trace_event(name + "/tdo", &group->tdo_trace, 1);
        traces.new_trace_event(name + "/tms", &group->tms_trace, 1);
        traces.new_trace_event(name + "/trst", &group->trst_trace, 1);

        nb_itf++;
      }
      else if (type == "cpi")
      {
        Cpi_group *group = new Cpi_group(name);
        new_master_port(name, &group->master);
        new_slave_port(name + "_pad", &group->slave);
        group->slave.set_sync_meth_muxed(&padframe::cpi_sync, nb_itf);
        group->slave.set_sync_cycle_meth_muxed(&padframe::cpi_sync_cycle, nb_itf);
        this->groups.push_back(group);
        traces.new_trace_event(name + "/pclk", &group->pclk_trace, 1);
        traces.new_trace_event(name + "/href", &group->href_trace, 1);
        traces.new_trace_event(name + "/vsync", &group->vsync_trace, 1);
        traces.new_trace_event(name + "/data", &group->data_trace, 8);
        nb_itf++;
      }
      else if (type == "uart")
      {
        Uart_group *group = new Uart_group(name);
        new_master_port(name + "_pad", &group->master);
        new_slave_port(name, &group->slave);
        group->master.set_sync_meth_muxed(&padframe::uart_master_sync, nb_itf);
        group->slave.set_sync_meth_muxed(&padframe::uart_chip_sync, nb_itf);
        this->groups.push_back(group);
        traces.new_trace_event(name + "/tx", &group->tx_trace, 1);
        traces.new_trace_event(name + "/rx", &group->rx_trace, 1);
        nb_itf++;
      }
      else if (type == "i2s")
      {
        I2s_group *group = new I2s_group(name);
        new_slave_port(name + "_pad", &group->slave);
        new_master_port(name, &group->master);
        group->master.set_sync_meth_muxed(&padframe::i2s_internal_edge, nb_itf);
        group->slave.set_sync_meth_muxed(&padframe::i2s_external_edge, nb_itf);
        group->sck_in = 2;
        group->ws_in = 2;
        group->sdi_in = 2;
        group->sdo_in = 2;
        group->sck_out = 2;
        group->ws_out = 2;
        group->sdi_out = 2;
        group->sdo_out = 2;
        this->groups.push_back(group);
        traces.new_trace_event(name + "/sck", &group->sck_trace, 1);
        traces.new_trace_event(name + "/ws", &group->ws_trace, 1);
        traces.new_trace_event(name + "/sdi", &group->sdi_trace, 1);
        traces.new_trace_event(name + "/sdo", &group->sdo_trace, 1);
        nb_itf++;
      }
      else if (type == "i2c")
      {
        I2c_group *group = new I2c_group(name);
        new_master_port(name + "_pad", &group->master);
        new_slave_port(name, &group->slave);
        group->master.set_sync_meth_muxed(&padframe::i2c_master_sync, nb_itf);
        group->slave.set_sync_meth_muxed(&padframe::i2c_chip_sync, nb_itf);
        this->groups.push_back(group);
        traces.new_trace_event(name + "/scl", &group->scl_trace, 1);
        traces.new_trace_event(name + "/sda", &group->sda_trace, 1);
        nb_itf++;
      }
      else if (type == "hyper")
      {
        Hyper_group *group = new Hyper_group(name);
        new_slave_port(name, &group->slave);
        group->slave.set_sync_cycle_meth_muxed(&padframe::hyper_sync_cycle, nb_itf);
        group->slave.set_cs_sync_meth_muxed(&padframe::hyper_cs_sync, nb_itf);
        this->groups.push_back(group);
        traces.new_trace_event(name + "/data", &group->data_trace, 8);
        js::Config *nb_cs_config = config->get("nb_cs");
        group->nb_cs = nb_cs_config ? nb_cs_config->get_int() : 1;
        for (int i=0; i<group->nb_cs; i++)
        {
          vp::Trace *trace = new vp::Trace;
          traces.new_trace_event(name + "/cs_" + std::to_string(i), trace, 1);
          group->cs_trace.push_back(trace);
          vp::HyperMaster *itf = new vp::HyperMaster;
          itf->set_sync_cycle_meth_muxed(&padframe::hyper_master_sync_cycle, nb_itf);

          new_master_port(name + "_cs" + std::to_string(i) + "_data_pad", itf);
          group->master.push_back(itf);

          vp::WireMaster<bool> *cs_itf = new vp::WireMaster<bool>;
          new_master_port(name + "_cs" + std::to_string(i) + "_pad", cs_itf);
          group->cs_master.push_back(cs_itf);
        }
        nb_itf++;
      }
      else if (type == "wire")
      {
        Wire_group *group = new Wire_group(name);
        this->groups.push_back(group);
        js::Config *is_master_config = config->get("is_master");
        js::Config *is_slave_config = config->get("is_slave");

        if (is_master_config != NULL && is_master_config->get_bool())
        {
          group->master.set_sync_meth_muxed(&padframe::master_wire_sync, nb_itf);
          this->new_master_port(name + "_pad", &group->master);
          group->slave.set_sync_meth_muxed(&padframe::wire_sync, nb_itf);
          this->new_slave_port(name, &group->slave);
        }
        else if (is_slave_config != NULL && is_slave_config->get_bool())
        {
          group->master.set_sync_meth_muxed(&padframe::master_wire_sync, nb_itf);
          this->new_master_port(name, &group->master);
          group->slave.set_sync_meth_muxed(&padframe::wire_sync, nb_itf);
          this->new_slave_port(name + "_pad", &group->slave);
        }

        nb_itf++;
      }
      else if (type == "gpio")
      {
        Gpio_group *group = new Gpio_group(name);
        this->groups.push_back(group);
        js::Config *is_master_config = config->get("is_master");
        js::Config *is_slave_config = config->get("is_slave");

        if (is_master_config != NULL && is_master_config->get_bool())
        {
          group->master.set_sync_meth_muxed(&padframe::master_gpio_sync, nb_itf);
          this->new_master_port(name + "_pad", &group->master);
        }

        nb_itf++;
      }
      else
      {
        trace.warning("Unknown pad group type (group: %s, type: %s)\n",
          name.c_str(), type.c_str());
      }
    }
  }


}


void padframe::set_pad(int *padin_value, int *padout_value, int *pad_value)
{
    if (*padin_value == 3 || *padout_value == 3)
    {
        *pad_value = 3;
    }
    else if (*padin_value == 2 || *padout_value == 2)
    {
        if (*padin_value == 2)
        {
            *pad_value = *padout_value;
        }
        else
        {
            *pad_value = *padin_value;
        }
    }
    else
    {
        if (*padin_value != *padout_value)
        {
            *pad_value = 3;
        }
        else
        {
            *pad_value = *padin_value;
        }
    }
}


void padframe::qspim_sync(void *__this, int sck, int data_0, int data_1, int data_2, int data_3, int mask, int id)
{
  padframe *_this = (padframe *)__this;
  Qspim_group *group = static_cast<Qspim_group *>(_this->groups[id]);
  unsigned int data = (data_0 << 0) | (data_1 << 1) | (data_2 << 2)| (data_3 << 3);

  if (mask & (1<<0))
    group->data_0_trace.event((uint8_t *)&data_0);
  if (mask & (1<<1))
    group->data_1_trace.event((uint8_t *)&data_1);
  if (mask & (1<<2))
    group->data_2_trace.event((uint8_t *)&data_2);
  if (mask & (1<<3))
    group->data_3_trace.event((uint8_t *)&data_3);

  if (group->active_cs == -1)
  {
    vp_warning_always(&_this->trace, "Trying to send QSPIM stream while no cs is active\n");
  }
  else if (!group->master[group->active_cs]->is_bound())
  {
    vp_warning_always(&_this->trace, "Trying to send QSPIM stream while pad is not connected (interface: %s)\n", group->name.c_str());
  }
  else
  {
    group->master[group->active_cs]->sync(sck, data_0, data_1, data_2, data_3, mask);
  }
}


void padframe::qspim_cs_sync(void *__this, int cs, int active, int id)
{
  padframe *_this = (padframe *)__this;
  Qspim_group *group = static_cast<Qspim_group *>(_this->groups[id]);

  if (cs >= group->nb_cs)
  {
    vp_warning_always(&_this->trace, "Trying to activate invalid cs (interface: %s, cs: %d, nb_cs: %d)\n", group->name.c_str(), cs, group->nb_cs);
    return;
  }

  group->cs_trace[cs]->event((uint8_t *)&active);
  group->active_cs = active ? cs : -1;

  if (!group->cs_master[cs]->is_bound())
  {
    vp_warning_always(&_this->trace, "Trying to send QSPIM stream while cs pad is not connected (interface: %s, cs: %d)\n", group->name.c_str(), cs);
  }
  else
  {
    group->cs_master[cs]->sync(!active);
  }
} 

void padframe::qspim_master_sync(void *__this, int sck, int data_0, int data_1, int data_2, int data_3, int mask, int id)
{
  padframe *_this = (padframe *)__this;
  Qspim_group *group = static_cast<Qspim_group *>(_this->groups[id]);

  if (mask & (1<<0))
    group->data_0_trace.event((uint8_t *)&data_0);
  if (mask & (1<<1))
    group->data_1_trace.event((uint8_t *)&data_1);
  if (mask & (1<<2))
    group->data_2_trace.event((uint8_t *)&data_2);
  if (mask & (1<<3))
    group->data_3_trace.event((uint8_t *)&data_3);

  group->slave.sync(sck, data_0, data_1, data_2, data_3, mask);
}



void padframe::jtag_pad_slave_sync(void *__this, int tck, int tdi, int tms, int trst, int id)
{
  padframe *_this = (padframe *)__this;
  Jtag_group *group = static_cast<Jtag_group *>(_this->groups[id]);

  group->tck_trace.event((uint8_t *)&tck);
  group->tdi_trace.event((uint8_t *)&tdi);
  group->tms_trace.event((uint8_t *)&tms);
  group->trst_trace.event((uint8_t *)&trst);

  group->master.sync(tck, tdi, tms, trst);
}


void padframe::jtag_pad_slave_sync_cycle(void *__this, int tdi, int tms, int trst, int id)
{
  padframe *_this = (padframe *)__this;
  Jtag_group *group = static_cast<Jtag_group *>(_this->groups[id]);

  group->tdi_trace.event((uint8_t *)&tdi);
  group->tms_trace.event((uint8_t *)&tms);
  group->trst_trace.event((uint8_t *)&trst);

  group->master.sync_cycle(tdi, tms, trst);
}



void padframe::jtag_chip_slave_sync(void *__this, int tck, int tdi, int tms, int trst, int id)
{
  padframe *_this = (padframe *)__this;
  Jtag_group *group = static_cast<Jtag_group *>(_this->groups[id]);

  group->tdo_trace.event((uint8_t *)&tdi);

  group->pad_slave.sync(tdi);
}


void padframe::jtag_chip_slave_sync_cycle(void *__this, int tdi, int tms, int trst, int id)
{
  padframe *_this = (padframe *)__this;
  Jtag_group *group = static_cast<Jtag_group *>(_this->groups[id]);

  group->tdo_trace.event((uint8_t *)&tdi);

  group->pad_slave.sync(tdi);
}



void padframe::cpi_sync(void *__this, int pclk, int href, int vsync, int data, int id)
{
  padframe *_this = (padframe *)__this;
  Cpi_group *group = static_cast<Cpi_group *>(_this->groups[id]);

  group->pclk_trace.event((uint8_t *)&pclk);
  group->href_trace.event((uint8_t *)&href);
  group->vsync_trace.event((uint8_t *)&vsync);
  group->data_trace.event((uint8_t *)&data);

  group->master.sync(pclk, href, vsync, data);
}


void padframe::cpi_sync_cycle(void *__this, int href, int vsync, int data, int id)
{
  padframe *_this = (padframe *)__this;
  Cpi_group *group = static_cast<Cpi_group *>(_this->groups[id]);

  group->href_trace.event((uint8_t *)&href);
  group->vsync_trace.event((uint8_t *)&vsync);
  group->data_trace.event((uint8_t *)&data);

  group->master.sync_cycle(href, vsync, data);
}


void padframe::uart_chip_sync(void *__this, int data, int id)
{
  padframe *_this = (padframe *)__this;
  Uart_group *group = static_cast<Uart_group *>(_this->groups[id]);
  group->tx_trace.event((uint8_t *)&data);
  if (!group->master.is_bound())
  {
    vp_warning_always(&_this->trace, "Trying to send UART stream while pad is not connected (interface: %s)\n", group->name.c_str());
  }
  else
  {
    group->master.sync(data);
  }
}



void padframe::uart_master_sync(void *__this, int data, int id)
{
  padframe *_this = (padframe *)__this;
  Uart_group *group = static_cast<Uart_group *>(_this->groups[id]);

  group->rx_trace.event((uint8_t *)&data);

  group->slave.sync(data);
}



void padframe::i2s_internal_edge(void *__this, int sck, int ws, int sd, bool full_duplex, int id)
{
  padframe *_this = (padframe *)__this;
  I2s_group *group = static_cast<I2s_group *>(_this->groups[id]);
  int sdi = sd & 0x3;
  int sdo = sd >> 2;

  group->sck_in = sck;
  group->ws_in = ws;
  group->sdi_in = sdi;
  group->sdo_in = sdo;

  _this->set_pad(&group->sck_in, &group->sck_out, &sck);
  _this->set_pad(&group->ws_in, &group->ws_out, &ws);
  _this->set_pad(&group->sdi_in, &group->sdi_out, &sdi);
  _this->set_pad(&group->sdo_in, &group->sdo_out, &sdo);

  group->sck_trace.event((uint8_t *)&sck);
  group->ws_trace.event((uint8_t *)&ws);
  group->sdi_trace.event((uint8_t *)&sdi);
  group->sdo_trace.event((uint8_t *)&sdo);

  sd = sdi | (sdo << 2);

  if (!group->slave.is_bound())
  {
    //vp_warning_always(&_this->trace, "Trying to send I2S stream while pad is not connected (interface: %s)\n", group->name.c_str());
  }
  else
  {
    group->slave.sync(sck, ws, sd, full_duplex);
  }
}



void padframe::i2s_external_edge(void *__this, int sck, int ws, int sd, bool full_duplex, int id)
{
  padframe *_this = (padframe *)__this;
  I2s_group *group = static_cast<I2s_group *>(_this->groups[id]);
  int sdi = sd & 0x3;
  int sdo = sd >> 2;

  group->sck_out = sck;
  group->ws_out = ws;
  group->sdi_out = sdi;
  group->sdo_out = sdo;

  _this->set_pad(&group->sck_in, &group->sck_out, &sck);
  _this->set_pad(&group->ws_in, &group->ws_out, &ws);
  _this->set_pad(&group->sdi_in, &group->sdi_out, &sdi);
  _this->set_pad(&group->sdo_in, &group->sdo_out, &sdo);

  group->sck_trace.event((uint8_t *)&sck);
  group->ws_trace.event((uint8_t *)&ws);
  group->sdi_trace.event((uint8_t *)&sdi);
  group->sdo_trace.event((uint8_t *)&sdo);

  sd = sdi | (sdo << 2);

  group->master.sync(sck, ws, sd, full_duplex);

  // Resynchronized the pad value outside after they have been resolved between internal and external state
  if (group->slave.is_bound())
  {
    group->slave.sync(sck, ws, sd, full_duplex);
  }
}



void padframe::i2c_chip_sync(void *__this, int scl, int sda, int id)
{
  padframe *_this = (padframe *)__this;
  I2c_group *group = static_cast<I2c_group *>(_this->groups[id]);
  group->scl_trace.event((uint8_t *)&scl);
  group->sda_trace.event((uint8_t *)&sda);
  if (!group->master.is_bound())
  {
    vp_warning_always(&_this->trace, "Trying to send I2C stream while pad is not connected (interface: %s)\n", group->name.c_str());
  }
  else
  {
    group->master.sync(scl, sda);
  }
}

void padframe::i2c_master_sync(void *__this, int scl, int sda, int id)
{
  padframe *_this = (padframe *)__this;
  I2c_group *group = static_cast<I2c_group *>(_this->groups[id]);

  group->sda_trace.event((uint8_t *)&sda);

  group->slave.sync(scl, sda);
}




void padframe::hyper_master_sync_cycle(void *__this, int data, int id)
{
  padframe *_this = (padframe *)__this;

  Hyper_group *group = static_cast<Hyper_group *>(_this->groups[id]);
  group->data_trace.event((uint8_t *)&data);
  group->slave.sync_cycle(data);
}

void padframe::hyper_sync_cycle(void *__this, int data, int id)
{
  padframe *_this = (padframe *)__this;
  Hyper_group *group = static_cast<Hyper_group *>(_this->groups[id]);
  group->data_trace.event((uint8_t *)&data);
  if (!group->master[group->active_cs]->is_bound())
  {
    vp_warning_always(&_this->trace, "Trying to send HYPER stream while pad is not connected (interface: %s)\n", group->name.c_str());
  }
  else
  {
    group->master[group->active_cs]->sync_cycle(data);
  }
}


void padframe::hyper_cs_sync(void *__this, int cs, int active, int id)
{
  padframe *_this = (padframe *)__this;
  Hyper_group *group = static_cast<Hyper_group *>(_this->groups[id]);

  if (cs >= group->nb_cs)
  {
    vp_warning_always(&_this->trace, "Trying to activate invalid cs (interface: %s, cs: %d, nb_cs: %d)\n", group->name.c_str(), cs, group->nb_cs);
    return;
  }

  group->cs_trace[cs]->event((uint8_t *)&active);
  group->active_cs = cs;

  if (!group->master[cs]->is_bound())
  {
    vp_warning_always(&_this->trace, "Trying to send HYPER stream while pad is not connected (interface: %s)\n", group->name.c_str());
  }
  else
  {
    group->master[cs]->cs_sync(cs, !active);
  }
}

void padframe::master_gpio_sync(void *__this, int value, int id)
{
  padframe *_this = (padframe *)__this;
  Gpio_group *group = static_cast<Gpio_group *>(_this->groups[id]);
  group->master.sync(value);  
}

void padframe::gpio_sync(void *__this, int value, int id)
{
  padframe *_this = (padframe *)__this;
  Gpio_group *group = static_cast<Gpio_group *>(_this->groups[id]);
  group->master.sync(value);  
}

void padframe::master_wire_sync(void *__this, int value, int id)
{
  padframe *_this = (padframe *)__this;
  Wire_group *group = static_cast<Wire_group *>(_this->groups[id]);
  group->slave.sync(value);  
}

void padframe::wire_sync(void *__this, int value, int id)
{
  padframe *_this = (padframe *)__this;
  Wire_group *group = static_cast<Wire_group *>(_this->groups[id]);
  group->master.sync(value);  
}


vp::IoReqStatus padframe::req(void *__this, vp::IoReq *req)
{
  padframe *_this = (padframe *)__this;

  uint64_t offset = req->get_addr();
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();

  _this->trace.msg("IO access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, req->get_is_write());

  return vp::IO_REQ_OK;
}

void padframe::ref_clock_sync(void *__this, bool value)
{
  padframe *_this = (padframe *)__this;
  _this->ref_clock_trace.event((uint8_t *)&value);
  _this->ref_clock_itf.sync(value);
}

void padframe::ref_clock_set_frequency(void *__this, int64_t value)
{
  padframe *_this = (padframe *)__this;
  _this->ref_clock_itf.set_frequency(value);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new padframe(config);
}
