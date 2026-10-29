// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Hardware mutex unit: lock (sleep when held) / unlock (transfer to a waiter).

#include "event_unit.hpp"

Mutex_unit::Mutex_unit(Event_unit *top)
: top(top)
{
  top->traces.new_trace("mutex/trace", &trace, vp::DEBUG);
  nb_mutexes = top->get_js_config()->get_child_int("**/properties/mutex/nb_mutexes");
  mutex_event = top->get_js_config()->get_child_int("**/properties/events/mutex");
  mutexes = new Mutex[nb_mutexes];
}


vp::IoReqStatus Mutex_unit::enqueue_sleep(Mutex *mutex, vp::IoReq *req, int core_id) {
  Core_event_unit *core_eu = &top->core_eu[core_id];

  // Enqueue the request so that the core can be unstalled when a value is pushed
  mutex->waiting_reqs[core_id] = req;
  mutex->waiting_mask |= 1<<core_id;

  // Don't forget to clear the event after wake-up by the mutex event
  core_eu->clear_evt_mask = 1<<mutex_event;

  return core_eu->wait_event(req);
}


void Mutex_unit::reset()
{
  for (int i=0; i<nb_mutexes; i++)
  {
    mutexes[i].reset();
  }
}

void Mutex::reset()
{
  locked = false;
  waiting_mask = 0;
  elected = -1;
  elect_cycle = -1;
}


// Give the mutex to `core`, which reads it with `req`: the lock read
// always completes through the mutex event of the core, answered the cycle
// after the election (hw_mutex_unit raises the event in the cycle of the
// election, the event unit grants the read the cycle after).
vp::IoReqStatus Mutex_unit::give(Mutex *mutex, vp::IoReq *req, int core_id)
{
  Core_event_unit *core_eu = &top->core_eu[core_id];
  mutex->locked = true;
  mutex->elected = core_id;
  mutex->elect_cycle = top->clock.get_cycles();
  *(uint32_t *)req->get_data() = mutex->value;
  core_eu->clear_evt_mask = 1<<mutex_event;
  vp::IoReqStatus status = core_eu->put_to_sleep(req);
  top->trigger_event(1<<mutex_event, 1<<core_id);
  return status;
}

vp::IoReqStatus Mutex_unit::req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data, int core)
{
  unsigned int id = EU_MUTEX_AREA_MUTEXID_GET(offset);
  offset = offset - (id << 2);
  if (id >= nb_mutexes) return eu_io_resp(req, vp::IO_RESP_INVALID);

  Mutex *mutex = &mutexes[id];

  top->trace.msg("Received mutex IO access (offset: 0x%x, mutex: %d, is_write: %d)\n", offset, id, is_write);

  if (!is_write)
  {
    if (!mutex->locked)
    {
      top->trace.msg("Locking mutex (mutex: %d, coreId: %d)\n", id, core);
      return this->give(mutex, req, core);
    }
    else if (mutex->elect_cycle == top->clock.get_cycles() && core < mutex->elected &&
      top->core_eu[mutex->elected].unwake(mutex_event))
    {
      // Asking in the cycle the mutex was given to a higher core, which has
      // not been answered yet: the election goes to this one
      int prev = mutex->elected;
      Core_event_unit *prev_eu = &top->core_eu[prev];
      top->trace.msg("Taking mutex over (mutex: %d, coreId: %d, fromCore: %d)\n", id, core, prev);
      mutex->waiting_reqs[prev] = prev_eu->pending_req_get();
      mutex->waiting_mask |= 1<<prev;
      return this->give(mutex, req, core);
    }
    else
    {
      top->trace.msg("Mutex already locked, waiting (mutex: %d, coreId: %d)\n", id, core);
      return enqueue_sleep(mutex, req, core);
    }
  }
  else
  {
    mutex->value = *(uint32_t *)req->get_data();
    unsigned int waiting_mask = mutex->waiting_mask;
    if (waiting_mask)
    {
      int i = __builtin_ctz(waiting_mask);
      top->trace.msg("Transfering mutex lock (mutex: %d, fromCore: %d, toCore: %d)\n", id, core, i);
      vp::IoReq *waiting_req = mutex->waiting_reqs[i];
      mutex->waiting_mask &= ~(1<<i);
      mutex->elected = i;
      mutex->elect_cycle = top->clock.get_cycles();
      *(uint32_t *)waiting_req->get_data() = mutex->value;
      top->trigger_event(1<<mutex_event, 1<<i);
    }
    else
    {
      top->trace.msg("Unlocking mutex (mutex: %d, coreId: %d)\n", id, core);
      mutex->locked = false;
    }
  }

  return eu_io_resp(req, vp::IO_RESP_OK);
}
