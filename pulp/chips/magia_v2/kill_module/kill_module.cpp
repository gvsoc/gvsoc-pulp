/*
 * Copyright (C) 2025 Fondazione Chips-IT
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
 * Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <stdio.h>

class KillModule : public vp::Component
{

public:

  KillModule(vp::ComponentConf &config);

  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
  static void done_fsm_handler(vp::Block *__this, vp::ClockEvent *event);

private:

  vp::Trace     trace;
  vp::IoSlave in;
  vp::WireMaster<bool> irq_done;

  uint64_t kill_base_address;
  uint64_t kill_addr_size;
  int nb_cores_to_wait;
  int nb_recv_kill_reqs;
  bool done_irq_enable;

  vp::ClockEvent *irq_done_event;

};

KillModule::KillModule(vp::ComponentConf &config)
: vp::Component(config)
{
  this->traces.new_trace("trace", &trace, vp::DEBUG);
  this->in.set_req_meth(&KillModule::req);
  this->new_slave_port("input", &this->in, this);
  this->new_master_port("irq_done", &this->irq_done);

  this->kill_base_address = get_js_config()->get("kill_addr_base")->get_int();
  this->kill_addr_size = get_js_config()->get("kill_addr_size")->get_int();
  this->nb_cores_to_wait = get_js_config()->get("nb_cores_to_wait")->get_int();
  this->done_irq_enable = get_js_config()->get("done_irq_enable")->get_bool();
  this->irq_done_event = this->event_new(&KillModule::done_fsm_handler);

  this->nb_recv_kill_reqs=0;
}

void KillModule::done_fsm_handler(vp::Block *__this, vp::ClockEvent *event) {
    KillModule *_this = (KillModule *)__this;

    _this->irq_done.sync(false);
    _this->trace.msg("Kill done irq reset\n");
}

vp::IoReqStatus KillModule::req(vp::Block *__this, vp::IoReq *req)
{
    KillModule *_this = (KillModule *)__this;

    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();
    uint8_t *data = req->get_data();
    uint32_t cnf_w = 0;

    if ((!is_write) || (size>4))
      return vp::IO_REQ_INVALID;
    else {

      if ((offset>=_this->kill_base_address) && (offset<=(_this->kill_base_address)+_this->kill_addr_size)) {
        memcpy((uint8_t*)&cnf_w, data, size);
        _this->nb_recv_kill_reqs++;
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received kill request at address 0x%08lx. Current kill count is %d. Number of cores to wait is %d. Received exit code is %d.\n",offset,_this->nb_recv_kill_reqs,_this->nb_cores_to_wait, cnf_w);
      }

      if (_this->nb_recv_kill_reqs==_this->nb_cores_to_wait) {
        _this->nb_recv_kill_reqs=0;
        if (_this->done_irq_enable) {
          _this->irq_done.sync(true);
          _this->event_enqueue(_this->irq_done_event, 1);
        }
        else {
          _this->time.get_engine()->quit(cnf_w);
        }
      }
      return vp::IO_REQ_OK;
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new KillModule(config);
}
