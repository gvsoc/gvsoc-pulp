// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// SoC-event FIFO: collects events arriving from the SoC side (wire) and lets
// the cluster pop them; raises the FIFO event while non-empty.

#include "event_unit.hpp"

Soc_event_unit::Soc_event_unit(Event_unit *top) : top(top)
{
  this->nb_fifo_events = top->get_js_config()->get_child_int("**/nb_fifo_events");
  this->fifo_soc_event = top->get_js_config()->get_child_int("**/fifo_event");

  top->traces.new_trace("soc_eu/trace", &trace, vp::DEBUG);

  this->fifo_event = new int[nb_fifo_events];

  this->soc_event_itf.set_sync_meth(&Soc_event_unit::sync);
  top->new_slave_port("soc_event", &this->soc_event_itf, (vp::Block *)this);

  this->reset();
}

void Soc_event_unit::reset()
{
  this->nb_free_events = this->nb_fifo_events;
  this->fifo_event_head = 0;
  this->fifo_event_tail = 0;
}

void Soc_event_unit::check_state()
{
  if (this->fifo_soc_event != -1 && this->nb_free_events != this->nb_fifo_events) {
    this->trace.msg("Generating FIFO event (id: %d)\n", this->fifo_soc_event);
    this->top->trigger_event(1<<this->fifo_soc_event, -1);
  }
}

void Soc_event_unit::sync(vp::Block *__this, int event)
{
  Soc_event_unit *_this = (Soc_event_unit *)__this;
  _this->trace.msg("Received soc event (event: %d)\n", event);

  if (_this->nb_free_events == 0) {
    return;
  }

  _this->nb_free_events--;
  _this->fifo_event[_this->fifo_event_head] = event;
  _this->fifo_event_head++;
  if (_this->fifo_event_head == _this->nb_fifo_events) _this->fifo_event_head = 0;

  _this->check_state();
}

vp::IoReqStatus Soc_event_unit::ioReq(vp::IoReq *req, uint32_t offset, bool is_write, uint32_t *data)
{
  if (is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);

  if (nb_free_events == nb_fifo_events) {
    trace.msg("Reading FIFO with no event\n");
    *data = 0;
  } else {
    trace.msg("Popping event from FIFO (id: %d)\n", fifo_event[fifo_event_tail]);
    *data = (1 << EU_SOC_EVENTS_VALID_BIT) | fifo_event[fifo_event_tail];
    fifo_event_tail++;
    if (fifo_event_tail == nb_fifo_events) fifo_event_tail = 0;
    nb_free_events++;
    if (nb_free_events != nb_fifo_events)
    {
      this->trace.msg("Generating FIFO soc event (id: %d)\n", this->fifo_soc_event);
      this->top->trigger_event(1<<this->fifo_soc_event, -1);
    }
  }

  return eu_io_resp(req, vp::IO_RESP_OK);
}
