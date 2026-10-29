// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Per-core event unit: event status/mask registers, event-wait (clock-gated
// sleep) and the wake path. A parked request returns IO_REQ_GRANTED and is
// answered later with resp() on the IoSlave that received it (resp_port).

#include "event_unit.hpp"

void Core_event_unit::build(Event_unit *top, int core_id)
{
  this->top = top;
  this->core_id = core_id;

  this->is_active.emplace(*this->top, "core_" + std::to_string(core_id) + "/active", 1, true, 1);

  // Per-core demux slave, muxed by core id (the third arg of demux_req).
  this->demux_in = new vp::IoSlave(core_id, &Event_unit::demux_req);
  top->new_slave_port("demux_in_" + std::to_string(core_id), this->demux_in);

  wakeup_event = top->event_new((vp::Block *)this, Core_event_unit::wakeup_handler);
  irq_wakeup_event = top->event_new((vp::Block *)this, Core_event_unit::irq_wakeup_handler);

  top->new_master_port("irq_req_" + std::to_string(core_id), &irq_req_itf);

  top->new_master_port("clock_" + std::to_string(core_id), &clock_itf);

  irq_ack_itf.set_sync_meth_muxed(&Event_unit::irq_ack_sync, core_id);
  top->new_slave_port("irq_ack_" + std::to_string(core_id), &irq_ack_itf);

  for (int i=0; i<32; i++)
  {
    in_event_itf[i].set_sync_meth_muxed(&Event_unit::in_event_sync, (core_id << 16 | i));
    top->new_slave_port("in_event_" + std::to_string(i) + "_pe_" + std::to_string(core_id), &in_event_itf[i]);
  }

}

vp::IoReqStatus Core_event_unit::req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data)
{
  if (offset == EU_CORE_BUFFER)
  {
    if (is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    *data = status;
    return eu_io_resp(req, vp::IO_RESP_OK);
  }
  else if (offset == EU_CORE_BUFFER_MASKED)
  {
    if (is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    *data = status & evt_mask;
    return eu_io_resp(req, vp::IO_RESP_OK);
  }
  else if (offset == EU_CORE_BUFFER_CLEAR)
  {
    if (!is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    clear_status(*data);
    top->trace.msg("Clearing buffer status (mask: 0x%x, newValue: 0x%x)\n", *data, status);
    check_state();
    return eu_io_resp(req, vp::IO_RESP_OK);
  }
  else if (offset == EU_CORE_MASK)
  {
    if (!is_write) *data = evt_mask;
    else {
      evt_mask = *data;
      top->trace.msg("Updating event mask (newValue: 0x%x)\n", *data);
      check_state();
    }
  }
  else if (offset == EU_CORE_MASK_AND)
  {
    if (!is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    evt_mask &= ~*data;
    top->trace.msg("Clearing event mask (mask: 0x%x, newValue: 0x%x)\n", *data, evt_mask);
    check_state();
  }
  else if (offset == EU_CORE_MASK_OR)
  {
    if (!is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    evt_mask |= *data;
    top->trace.msg("Setting event mask (mask: 0x%x, newValue: 0x%x)\n", *data, evt_mask);
    check_state();
  }
  else if (offset == EU_CORE_STATUS)
  {
    top->trace.force_warning("UNIMPLEMENTED at %s %d\n", __FILE__, __LINE__);
    return eu_io_resp(req, vp::IO_RESP_INVALID);
  }
  else if (offset == EU_CORE_EVENT_WAIT)
  {
    if (is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    top->trace.msg("Wait\n");
    // The return value (masked status) is filled by wait_event /
    // check_state at wake time. An event already buffered is answered at
    // once, as a plain load.
    pending_req_is_wait = true;
    return wait_event(req, CORE_STATE_WAITING_EVENT, true);
  }

  else if (offset == EU_CORE_EVENT_WAIT_CLEAR)
  {
    top->trace.msg("Wait and clear\n");
    clear_evt_mask = evt_mask;
    // The return value (masked status) is filled by wait_event /
    // check_state at wake time. An event already buffered is answered at
    // once, as a plain load.
    pending_req_is_wait = true;
    return wait_event(req, CORE_STATE_WAITING_EVENT, true);
  }
  else if (offset == EU_CORE_MASK_IRQ)
  {
    if (!is_write) *data = irq_mask;
    else {
      top->trace.msg("Updating irq mask (newValue: 0x%x)\n", *data);
      irq_mask = *data;
      check_state();
    }
  }
  else if (offset == EU_CORE_BUFFER_IRQ_MASKED)
  {
    if (is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    *data = status & irq_mask;
  }
  else if (offset == EU_CORE_MASK_IRQ_AND)
  {
    if (!is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    irq_mask &= ~*data;
    top->trace.msg("Clearing irq mask (mask: 0x%x, newValue: 0x%x)\n", *data, irq_mask);
    check_state();
  }
  else if (offset == EU_CORE_MASK_IRQ_OR)
  {
    if (!is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    irq_mask |= *data;
    top->trace.msg("Setting irq mask (mask: 0x%x, newValue: 0x%x)\n", *data, irq_mask);
    check_state();
  }
  else
  {
    return eu_io_resp(req, vp::IO_RESP_INVALID);
  }

  return eu_io_resp(req, vp::IO_RESP_OK);
}

// Legacy per-core static req handler. Inert in this model (the demux path is
// handled by Event_unit::demux_req); kept for declaration completeness.
vp::IoReqStatus Core_event_unit::req(vp::Block *__this, vp::IoReq *req)
{
  Event_unit *_this = (Event_unit *)__this;
  _this->trace.force_warning("UNIMPLEMENTED Core_event_unit::req\n");
  return eu_io_resp(req, vp::IO_RESP_INVALID);
}

void Core_event_unit::irq_ack_sync(int irq, int core)
{
  clear_status(1<<irq);
  sync_irq = -1;

  check_state();
}

void Core_event_unit::set_status(uint32_t new_value)
{
  status = new_value;
}

void Core_event_unit::clear_status(uint32_t mask)
{
  status = status & ~mask;
  top->soc_event_unit->check_state();
}

void Core_event_unit::check_pending_req()
{
  // io_v2: answer the parked request on the port that received it.
  this->resp_port->resp(this->pending_req);
}

void Core_event_unit::cancel_pending_req()
{
  pending_req = NULL;
}

void Core_event_unit::check_wait_mask()
{
  if (clear_evt_mask)
  {
    clear_status(clear_evt_mask);
    top->trace.msg("Clear event after wake-up (evtMask: 0x%x, status: 0x%x)\n", clear_evt_mask, status);
    clear_evt_mask = 0;
  }
}

vp::IoReqStatus Core_event_unit::put_to_sleep(vp::IoReq *req, Event_unit_core_state_e wait_state)
{
  state = wait_state;
  this->is_active->set(0);
  // clock_itf is the (optional) core clock-gating wire; the iss_v2 core has no
  // such input, so it is left unbound and the core stalls on the parked request
  // instead. Guard the sync so an unbound wire master is not invoked.
  if (this->clock_itf.is_bound()) this->clock_itf.sync(0);
  pending_req = req;
  return vp::IO_REQ_GRANTED;
}

vp::IoReqStatus Core_event_unit::wait_event(vp::IoReq *req, Event_unit_core_state_e wait_state,
  bool sync_if_ready)
{
  top->trace.msg("Wait request (status: 0x%x, evt_mask: 0x%x)\n", status, evt_mask);

  // With a masked event already in the buffer, event_unit_core grants the
  // read at once and answers it the next cycle, like any 1-cycle register read
  // (the core is not stopped), then clears the buffer if asked to.
  if (sync_if_ready && (evt_mask & status))
  {
    *(uint32_t *)req->get_data() = evt_mask & status;
    check_wait_mask();
    this->check_state();
    req->inc_latency(1);
    return eu_io_resp(req, vp::IO_RESP_OK);
  }

  // This takes a couple of cycles for the event unit to clock-gate the core
  // before replying, seen as a latency from the core point of view.
  req->inc_latency(EU_WAKEUP_REQ_LATENCY);

  // The core always goes to sleep even if the event is already there; this
  // gives much better timing results on barriers (matches the legacy model).
  if (evt_mask & status)
  {
    // The core asked for clock-gating but the event status prevents it. Still
    // apply the latency as the core goes to sleep before continuing.
    top->trace.msg("Activating clock (core: %d)\n", core_id);
    // Capture the wait return value (masked status) before the wait-clear
    // mask wipes the events.
    *(uint32_t *)req->get_data() = evt_mask & status;
    check_wait_mask();
    pending_req = req;
    top->event_enqueue(wakeup_event, EU_WAKEUP_LATENCY);
    return vp::IO_REQ_GRANTED;
  }
  else
  {
    return this->put_to_sleep(req, wait_state);
  }
}

bool Core_event_unit::unwake(int event)
{
  if (!this->wakeup_event->is_enqueued() || this->pending_req == NULL)
  {
    return false;
  }
  top->event_cancel(this->wakeup_event);
  this->state = CORE_STATE_WAITING_EVENT;
  this->clear_evt_mask = 1 << event;
  this->status &= ~(1 << event);
  return true;
}

void Core_event_unit::reset()
{
  status = 0;
  evt_mask = 0;
  irq_mask = 0;
  pending_req_is_wait = false;
  clear_evt_mask = 0;
  sync_irq = -1;
  pending_elw = false;
  state = CORE_STATE_NONE;
  if (this->clock_itf.is_bound()) this->clock_itf.sync(1);
}

void Core_event_unit::wakeup_handler(vp::Block *__this, vp::ClockEvent *event)
{
  Core_event_unit *_this = (Core_event_unit *)__this;
  _this->top->trace.msg("Replying to core after wakeup (core: %d)\n", _this->core_id);
  _this->is_active->set(1);
  if (_this->clock_itf.is_bound()) _this->clock_itf.sync(1);
  _this->check_pending_req();
  _this->check_state();
}

void Core_event_unit::irq_wakeup_handler(vp::Block *__this, vp::ClockEvent *event)
{
  Core_event_unit *_this = (Core_event_unit *)__this;
  _this->top->trace.msg("IRQ wakeup\n");
  _this->is_active->set(1);
  if (_this->clock_itf.is_bound()) _this->clock_itf.sync(1);
  _this->check_state();
}

void Core_event_unit::check_state()
{
  uint32_t status_irq_masked = status & irq_mask;
  uint32_t status_evt_masked = status & evt_mask;
  int irq = status_irq_masked ? 31 - __builtin_clz(status_irq_masked) : -1;

  top->trace.msg("Checking core state (coreId: %d, active: %d, status: 0x%x, evtMask: 0x%x, irqMask: 0x%x)\n", core_id, this->is_active->get(), status, evt_mask, irq_mask);

  if (this->is_active->get())
  {
    if (irq != sync_irq) {
      top->trace.msg("Updating irq req (core: %d, irq: %d)\n", core_id, irq);
      sync_irq = irq;
      irq_req_itf.sync(irq);
    }
  }

  if (!this->is_active->get())
  {
    if (status_irq_masked && !status_evt_masked)
    {
      // There is an active IRQ but no event, the core must be woken up just for
      // the duration of the IRQ handler. The elw instruction will replay the
      // access, so keep the state as it is to resume the on-going sync.
      top->trace.msg("Activating clock for IRQ handling(core: %d)\n", core_id);

      this->pending_elw = true;

      if (!irq_wakeup_event->is_enqueued())
      {
        top->event_enqueue(irq_wakeup_event, EU_WAKEUP_LATENCY);
        sync_irq = -1;
      }
    }
    else
    {
      if (status_evt_masked)
      {
        if (this->pending_elw)
        {
          this->set_state(CORE_STATE_SKIP_ELW);
        }
        else
        {
          switch (state)
          {
            case CORE_STATE_WAITING_EVENT:
            case CORE_STATE_WAITING_BARRIER:

              top->trace.msg("Activating clock (core: %d)\n", core_id);
              // Refresh the wait read's return value with the masked status
              // at wake time (the value stored at park time predates the
              // waking event), before the wait-clear mask wipes it. The
              // wait and barrier registers carry this semantic (a
              // barrier read returns the masked buffer too); dispatch and
              // mutex parked reads fill their own value.
              if (pending_req && ((state == CORE_STATE_WAITING_EVENT
                  && pending_req_is_wait) || state == CORE_STATE_WAITING_BARRIER))
              {
                *(uint32_t *)pending_req->get_data() = evt_mask & status;
              }
              state = CORE_STATE_NONE;
              check_wait_mask();
              if (!wakeup_event->is_enqueued())
              {
                top->event_enqueue(wakeup_event, EU_EVENT_WAKEUP_LATENCY + wakeup_delay);
              }
              break;

            default:
              break;
          }
        }
      }
    }
  }
}
