// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Hardware barrier unit: each core triggers its bit and waits; the last
// arrival releases the barrier by triggering the barrier event to the team.

#include "event_unit.hpp"

Barrier_unit::Barrier_unit(Event_unit *top)
: top(top)
{
  top->traces.new_trace("barrier/trace", &trace, vp::DEBUG);
  nb_barriers = top->get_js_config()->get_child_int("**/properties/barriers/nb_barriers");
  barrier_event = top->get_js_config()->get_child_int("**/properties/events/barrier");
  barriers = new Barrier[nb_barriers];
}

void Barrier_unit::check_barrier(int barrier_id)
{
  Barrier *barrier = &barriers[barrier_id];

  if (barrier->status == barrier->core_mask)
  {
    trace.msg("Barrier reached, triggering event (barrier: %d, coreMask: 0x%x, targetMask: 0x%x)\n", barrier_id, barrier->core_mask, barrier->target_mask);
    barrier->status = 0;

    top->trigger_event(1<<barrier_event, barrier->target_mask);
  }
}


vp::IoReqStatus Barrier_unit::req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data, int core)
{
  unsigned int barrier_id = EU_BARRIER_AREA_BARRIERID_GET(offset);
  offset = offset - EU_BARRIER_AREA_OFFSET_GET(barrier_id);
  if (barrier_id >= nb_barriers) return eu_io_resp(req, vp::IO_RESP_INVALID);
  Barrier *barrier = &barriers[barrier_id];

  if (offset == EU_HW_BARR_TRIGGER_MASK)
  {
    if (!is_write) *data = barrier->core_mask;
    else {
      trace.msg("Setting barrier core mask (barrier: %d, mask: 0x%x)\n", barrier_id, *data);
      barrier->core_mask = *data;
      check_barrier(barrier_id);
    }
  }

  else if (offset == EU_HW_BARR_TARGET_MASK)
  {
    if (!is_write) *data = barrier->target_mask;
    else {
      trace.msg("Setting barrier target mask (barrier: %d, mask: 0x%x)\n", barrier_id, *data);
      barrier->target_mask = *data;
      check_barrier(barrier_id);
    }
  }
  else if (offset == EU_HW_BARR_STATUS)
  {
    if (!is_write) *data = barrier->status;
    else {
      trace.msg("Setting barrier status (barrier: %d, status: 0x%x)\n", barrier_id, *data);
      barrier->status = *data;
      check_barrier(barrier_id);
    }
  }
  else if (offset == EU_HW_BARR_TRIGGER)
  {
    if (!is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    else {
      barrier->status |= *data;
      trace.msg("Barrier mask trigger (barrier: %d, mask: 0x%x, newStatus: 0x%x)\n", barrier_id, *data, barrier->status);
    }

    check_barrier(barrier_id);
  }
  else if (offset == EU_HW_BARR_TRIGGER_SELF)
  {
    // The access is valid only through the demux
    if (core == -1) return eu_io_resp(req, vp::IO_RESP_INVALID);
    barrier->status |= 1 << core;
    trace.msg("Barrier trigger (barrier: %d, coreId: %d, newStatus: 0x%x)\n", barrier_id, core, barrier->status);

    check_barrier(barrier_id);
  }
  else if (offset == EU_HW_BARR_TRIGGER_WAIT)
  {
    // The access is valid only through the demux
    if (core == -1) return eu_io_resp(req, vp::IO_RESP_INVALID);

    Core_event_unit *core_eu = &top->core_eu[core];
    if (core_eu->get_state() == CORE_STATE_WAITING_BARRIER)
    {
      // The core was already waiting for the barrier which means it was
      // interrupted. Just resume the barrier by going to sleep.
      trace.msg("Resuming barrier trigger and wait (barrier: %d, coreId: %d, newStatus: 0x%x)\n", barrier_id, core, barrier->status);
    }
    else
    {
      barrier->status |= 1 << core;
      trace.msg("Barrier trigger and wait (barrier: %d, coreId: %d, newStatus: 0x%x)\n", barrier_id, core, barrier->status);
    }

    check_barrier(barrier_id);

    return core_eu->wait_event(req, CORE_STATE_WAITING_BARRIER);
  }
  else if (offset == EU_HW_BARR_TRIGGER_WAIT_CLEAR)
  {
    // The access is valid only through the demux
    if (core == -1) return eu_io_resp(req, vp::IO_RESP_INVALID);

    Core_event_unit *core_eu = &top->core_eu[core];
    if (core_eu->get_state() == CORE_STATE_WAITING_BARRIER)
    {
      // The core was already waiting for the barrier which means it was
      // interrupted. Just resume the barrier by going to sleep.
      trace.msg("Resuming barrier trigger and wait (barrier: %d, coreId: %d, mask: 0x%x, newStatus: 0x%x)\n", barrier_id, core, barrier->core_mask, barrier->status);
    }
    else
    {
      barrier->status |= 1 << core;
      trace.msg("Barrier trigger, wait and clear (barrier: %d, coreId: %d, newStatus: 0x%x)\n", barrier_id, core, barrier->status);
    }
    core_eu->clear_evt_mask = core_eu->evt_mask;

    check_barrier(barrier_id);

    return core_eu->wait_event(req, CORE_STATE_WAITING_BARRIER);
  }
  else if (offset == EU_HW_BARR_STATUS_SUMMARY)
  {
    if (is_write) return eu_io_resp(req, vp::IO_RESP_INVALID);
    uint32_t status = 0;
    for (unsigned int i=1; i<nb_barriers; i++) status |= barriers[i].status;
    *data = status;
  }
  else return eu_io_resp(req, vp::IO_RESP_INVALID);

  return eu_io_resp(req, vp::IO_RESP_OK);
}

void Barrier_unit::reset()
{
  for (int i=0; i<nb_barriers; i++)
  {
    Barrier *barrier = &barriers[i];
    barrier->core_mask = 0;
    barrier->status = 0;
    barrier->target_mask = 0;
  }
}
