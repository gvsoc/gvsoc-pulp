// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Event unit top: register-area dispatch (main `in` port + per-core muxed
// `demux_in` fast path), software events, and event triggering.

#include "event_unit.hpp"

Event_unit::Event_unit(vp::ComponentConf &config)
: vp::Component(config)
{
  nb_core = get_js_config()->get_child_int("nb_core");

  traces.new_trace("trace", &trace, vp::DEBUG);

  new_slave_port("input", &in);

  core_eu = (Core_event_unit *)new Core_event_unit[nb_core];
  mutex = new Mutex_unit(this);
  dispatch = new Dispatch_unit(this);
  barrier_unit = new Barrier_unit(this);
  soc_event_unit = new Soc_event_unit(this);

  for (int i=0; i<nb_core; i++)
  {
    core_eu[i].build(this, i);
  }

}

void Event_unit::reset(bool active)
{
  if (active)
  {
    for (int i=0; i<nb_core; i++)
    {
      core_eu[i].reset();
    }
    dispatch->reset();
    barrier_unit->reset();
    mutex->reset();
    soc_event_unit->reset();
  }
}

vp::IoReqStatus Event_unit::req(vp::Block *__this, vp::IoReq *req)
{
  Event_unit *_this = (Event_unit *)__this;

  uint64_t offset = req->get_addr();
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();
  bool is_write = req->get_is_write();

  _this->trace.msg("Event_unit access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

  if (size != 4)
  {
    _this->trace.force_warning("Only 32 bits accesses are allowed\n");
    return eu_io_resp(req, vp::IO_RESP_INVALID);
  }

  if (offset >= EU_CORES_AREA_OFFSET && offset < EU_CORES_AREA_OFFSET + EU_CORES_AREA_SIZE)
  {
    unsigned int core_id = EU_CORE_AREA_COREID_GET(offset - EU_CORES_AREA_OFFSET);
    if (core_id >= _this->nb_core) return eu_io_resp(req, vp::IO_RESP_INVALID);
    // A blocking core access via the shared `in` port must be answered on `in`.
    _this->core_eu[core_id].resp_port = &_this->in;
    return _this->core_eu[core_id].req(req, offset - EU_CORE_AREA_OFFSET_GET(core_id), is_write, (uint32_t *)data);
  }
  else if (offset >= EU_SOC_EVENTS_AREA_OFFSET && offset < EU_SOC_EVENTS_AREA_OFFSET + EU_SOC_EVENTS_AREA_SIZE)
  {
    return _this->soc_event_unit->ioReq(req, offset - EU_SOC_EVENTS_AREA_OFFSET, is_write, (uint32_t *)data);
  }
  else if (offset >= EU_EXT_EVENT_AREA_OFFSET && offset < EU_EXT_EVENT_AREA_OFFSET + EU_EXT_EVENT_AREA_SIZE)
  {
    _this->trace.force_warning("UNIMPLEMENTED at %s %d\n", __FILE__, __LINE__);
  }
  else if (offset >= EU_SW_EVENTS_AREA_BASE && offset < EU_SW_EVENTS_AREA_BASE + EU_SW_EVENTS_AREA_SIZE)
  {
    return _this->sw_events_req(req, offset - EU_SW_EVENTS_AREA_BASE, is_write, (uint32_t *)data);
  }
  else if (offset >= EU_BARRIER_AREA_OFFSET && offset < EU_BARRIER_AREA_OFFSET + EU_BARRIER_AREA_SIZE)
  {
    _this->trace.force_warning("UNIMPLEMENTED at %s %d\n", __FILE__, __LINE__);
  }

  return eu_io_resp(req, vp::IO_RESP_INVALID);

}



vp::IoReqStatus Event_unit::sw_events_req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data)
{
  if (offset >= EU_CORE_TRIGG_SW_EVENT && offset <  EU_CORE_TRIGG_SW_EVENT_SIZE)
  {
    if (!is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);

    int event = (offset - EU_CORE_TRIGG_SW_EVENT) >> 2;
    trace.msg("SW event trigger (event: %d, coreMask: 0x%x)\n", event, *data);
    trigger_event(1<<event, *data);
  }
  else if (offset >= EU_CORE_TRIGG_SW_EVENT_WAIT && offset <  EU_CORE_TRIGG_SW_EVENT_WAIT_SIZE)
  {
    trace.force_warning("UNIMPLEMENTED at %s %d\n", __FILE__, __LINE__);
  }
  else if (offset >= EU_CORE_TRIGG_SW_EVENT_WAIT_CLEAR && offset <  EU_CORE_TRIGG_SW_EVENT_WAIT_CLEAR_SIZE)
  {
    trace.force_warning("UNIMPLEMENTED at %s %d\n", __FILE__, __LINE__);
  }
  else
  {
    trace.force_warning("UNIMPLEMENTED at %s %d\n", __FILE__, __LINE__);
    return eu_io_resp(req, vp::IO_RESP_INVALID);
  }

  return eu_io_resp(req, vp::IO_RESP_OK);
}


void Event_unit::trigger_event(int event_mask, uint32_t core_mask, int delay)
{
  for (unsigned int i=0; i<nb_core; i++) {
    if (core_mask == 0 || (core_mask & (1 << i))) {
      send_event(i, event_mask, delay);
    }
  }
}

void Event_unit::send_event(int core, uint32_t mask, int delay)
{
  trace.msg("Triggering event (core: %d, mask: 0x%x)\n", core, mask);
  Core_event_unit *eu = &core_eu[core];
  eu->set_status(eu->status | mask);
  eu->wakeup_delay = delay;
  eu->check_state();
  eu->wakeup_delay = 0;
}

void Event_unit::irq_ack_sync(vp::Block *__this, int irq, int core)
{
  Event_unit *_this = (Event_unit *)__this;

  _this->trace.msg("Received IRQ acknowledgement (core: %d, irq: %d)\n", core, irq);

  _this->core_eu[core].irq_ack_sync(irq, core);
}

vp::IoReqStatus Event_unit::demux_req(vp::Block *__this, vp::IoReq *req, int core)
{
  Event_unit *_this = (Event_unit *)__this;

  uint64_t offset = req->get_addr();
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();
  bool is_write = req->get_is_write();

  _this->trace.msg("Demux event_unit access (core: %d, offset: 0x%x, size: 0x%x, is_write: %d)\n", core, offset, size, is_write);

  Core_event_unit *core_eu = &_this->core_eu[core];

  // A blocking access via this core's demux port is answered on that port.
  core_eu->resp_port = core_eu->demux_in;

  core_eu->pending_elw = false;
  core_eu->pending_req_is_wait = false;

  if (core_eu->get_state() == CORE_STATE_SKIP_ELW)
  {
    core_eu->set_state(CORE_STATE_NONE);
    return eu_io_resp(req, vp::IO_RESP_OK);
  }

  if (size != 4)
  {
    _this->trace.force_warning("Only 32 bits accesses are allowed\n");
    return eu_io_resp(req, vp::IO_RESP_INVALID);
  }

  if (offset >= EU_CORE_DEMUX_OFFSET && offset < EU_CORE_DEMUX_OFFSET + EU_CORE_DEMUX_SIZE)
  {
    return _this->core_eu[core].req(req, offset - EU_CORE_DEMUX_OFFSET, is_write, (uint32_t *)data);
  }
  else if (offset >= EU_MUTEX_DEMUX_OFFSET && offset < EU_MUTEX_DEMUX_OFFSET + EU_MUTEX_DEMUX_SIZE)
  {
    return _this->mutex->req(req, offset - EU_MUTEX_DEMUX_OFFSET, is_write, (uint32_t *)data, core);
  }
  else if (offset >= EU_DISPATCH_DEMUX_OFFSET && offset < EU_DISPATCH_DEMUX_OFFSET + EU_DISPATCH_DEMUX_SIZE)
  {
    return _this->dispatch->req(req, offset - EU_DISPATCH_DEMUX_OFFSET, is_write, (uint32_t *)data, core);
  }
  else if (offset >= EU_LOOP_DEMUX_OFFSET && offset < EU_LOOP_DEMUX_OFFSET + EU_LOOP_DEMUX_SIZE)
  {
    _this->trace.force_warning("UNIMPLEMENTED at %s %d\n", __FILE__, __LINE__);
  }
  else if (offset >= EU_SW_EVENTS_DEMUX_OFFSET && offset < EU_SW_EVENTS_DEMUX_OFFSET + EU_SW_EVENTS_DEMUX_SIZE)
  {
    return _this->sw_events_req(req, offset - EU_SW_EVENTS_DEMUX_OFFSET, is_write, (uint32_t *)data);
  }
  else if (offset >= EU_BARRIER_DEMUX_OFFSET && offset < EU_BARRIER_DEMUX_OFFSET + EU_BARRIER_DEMUX_SIZE)
  {
    return _this->barrier_unit->req(req, offset - EU_BARRIER_DEMUX_OFFSET, is_write, (uint32_t *)data, core);
  }

  return eu_io_resp(req, vp::IO_RESP_INVALID);
}

void Event_unit::in_event_sync(vp::Block *__this, bool active, int id)
{
  Event_unit *_this = (Event_unit *)__this;
  int core_id = id >> 16;
  int event_id = id & 0xffff;
  _this->trace.msg("Received input event (core: %d, event: %d, active: %d)\n", core_id, event_id, active);
  Core_event_unit *eu = &_this->core_eu[core_id];
  eu->set_status(eu->status | (1<<event_id));
  eu->check_state();
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new Event_unit(config);
}
