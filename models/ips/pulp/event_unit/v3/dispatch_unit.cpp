// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Dispatch unit: a small FIFO the cores pop work items from (sleeping when
// empty), and a master pushes values into (waking the elected cores).

#include "event_unit.hpp"

vp::IoReqStatus Dispatch_unit::enqueue_sleep(Dispatch *dispatch, vp::IoReq *req, int core_id, bool is_caller) {
  Core_event_unit *core_eu = &top->core_eu[core_id];

  // Enqueue the request so that the core can be unstalled when a value is pushed
  dispatch->waiting_reqs[core_id] = req;
  dispatch->waiting_mask |= 1<<core_id;

  // Don't forget to clear the event after wake-up by the dispatch event
  core_eu->clear_evt_mask = 1<<dispatch_event;

  if (is_caller)
    return core_eu->wait_event(req);
  else
    return core_eu->put_to_sleep(req);
}


Dispatch_unit::Dispatch_unit(Event_unit *top)
: top(top)
{
  dispatch_event = top->get_js_config()->get_child_int("**/properties/events/dispatch");
  size = top->get_js_config()->get_child_int("**/properties/dispatch/size");
  core = new Dispatch_core[top->nb_core];
  dispatches = new Dispatch[size];
}

void Dispatch_unit::reset()
{
  fifo_head = 0;
  config = 0;
  for (int i=0; i<top->nb_core; i++)
  {
    core[i].tail = 0;
  }
  for (int i=0; i<size; i++)
  {
    dispatches[i].value = 0;
    dispatches[i].status_mask = 0;
    dispatches[i].config_mask = 0;
    dispatches[i].waiting_mask = 0;
  }
}

vp::IoReqStatus Dispatch_unit::req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data, int core_id)
{
  if (offset == EU_DISPATCH_FIFO_ACCESS)
  {
    if (is_write)
    {
      unsigned int id = fifo_head++;
      if (fifo_head == size) fifo_head = 0;

      Dispatch *dispatch = &dispatches[id];

      // When pushing to the FIFO, the global config is pushed to the elected dispatcher
      dispatch->config_mask = config;     // Cores that will get a valid value

      top->trace.msg("Pushing dispatch value (dispatch: %d, value: 0x%x, coreMask: 0x%x)\n", id, *data, dispatch->config_mask);

      // Case where the master push a value
      dispatch->value = *data;
      // Reinitialize the status mask to notify a new value is ready
      dispatch->status_mask = -1;
      // Then wake-up the waiting cores
      unsigned int mask = dispatch->waiting_mask & dispatch->status_mask;
      for (int i=0; i<32 && mask; i++)
      {
        if (mask & (1<<i))
        {
          // Only wake-up the core if he's actually involved in the team
          if (dispatch->config_mask & (1<<i))
          {
            top->trace.msg("Waking-up core waiting for dispatch value (coreId: %d)\n", i);
            vp::IoReq *waiting_req = dispatch->waiting_reqs[i];

            // Clear the status bit as the waking core takes the data
            dispatch->status_mask &= ~(1<<i);
            dispatch->waiting_mask &= ~(1<<i);

            // Store the dispatch value into the pending request. The reply
            // delay is introduced by the wakeup event.
            *(uint32_t *)waiting_req->get_data() = dispatch->value;

            // Update the core fifo
            core[i].tail++;
            if (core[i].tail == size) core[i].tail = 0;

            // Clear the mask to stop iterating early
            mask &= ~(1<<i);

            // And trigger the event to the core
            top->trigger_event(1<<dispatch_event, 1<<i);
          }
          // Otherwise keep him sleeping and increase its index so that he will bypass this entry when he wakes up
          else
          {
            // Cancel current dispatch sleep
            dispatch->status_mask &= ~(1<<i);
            dispatch->waiting_mask &= ~(1<<i);
            vp::IoReq *pending_req = dispatch->waiting_reqs[i];

            // Bypass the current entry
            core[i].tail++;
            if (core[i].tail == size) core[i].tail = 0;

            // And reenqueue to the next entry
            id = core[i].tail;
            enqueue_sleep(&dispatches[id], pending_req, i, false);
            top->trace.msg("Incrementing core counter to bypass entry (coreId: %d, newIndex: %d)\n", i, id);
          }
        }
      }

      return eu_io_resp(req, vp::IO_RESP_OK);
    }
    else
    {
      int id = core[core_id].tail;
      Dispatch *dispatch = &dispatches[id];

      top->trace.msg("Trying to get dispatch value (dispatch: %d)\n", id);

      // In case we found ready elements where this core is not involved, bypass them all
      while ((dispatch->status_mask & (1<<core_id)) && !(dispatch->config_mask & (1<<core_id))) {
        dispatch->status_mask &= ~(1<<core_id);
        core[core_id].tail++;
        if (core[core_id].tail == size) core[core_id].tail = 0;
        id = core[core_id].tail;
        dispatch = &dispatches[id];
        top->trace.msg("Incrementing core counter to bypass entry (coreId: %d, newIndex: %d)\n", core_id, id);
      }

      // Case where a slave tries to get a value
      if (dispatch->status_mask & (1<<core_id))
      {
        // A value is ready. Get it and clear the status bit to not read it again the next time
        // In case the core is not involved in this dispatch, returns 0
        if (dispatch->config_mask & (1<<core_id)) *data = dispatch->value;
        else *data = 0;
        dispatch->status_mask &= ~(1<<core_id);
        top->trace.msg("Getting ready dispatch value (dispatch: %d, value: %x, dispatchStatus: 0x%x)\n", id, dispatch->value, dispatch->status_mask);
        core[core_id].tail++;
        if (core[core_id].tail == size) core[core_id].tail = 0;
      }
      else
      {
        // Nothing is ready, go to sleep
        top->trace.msg("No ready dispatch value, going to sleep (dispatch: %d, value: %x, dispatchStatus: 0x%x)\n", id, dispatch->value, dispatch->status_mask);
        return enqueue_sleep(dispatch, req, core_id);
      }

      return eu_io_resp(req, vp::IO_RESP_OK);
    }
  }
  else if (offset == EU_DISPATCH_TEAM_CONFIG)
  {
    config = *data;
    return eu_io_resp(req, vp::IO_RESP_OK);
  }
  else
  {
    return eu_io_resp(req, vp::IO_RESP_INVALID);
  }
}
