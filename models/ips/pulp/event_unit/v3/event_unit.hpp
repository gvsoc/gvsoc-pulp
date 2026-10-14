// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// PULP cluster hardware event unit (io_v2), as event_unit_flex: core units,
// hardware mutexes, dispatch FIFO, barriers, software events and the SoC-event
// FIFO, without the semaphores and bitfields of the GAP9 one. Ported from the
// legacy `pulp.event_unit.eu_v3_impl` io-protocol model and split into
// one translation unit per sub-block. The behaviour is identical; the io->io_v2
// changes are:
//   * requests complete with eu_io_resp(req, status) returning IO_REQ_DONE,
//   * blocking ops return IO_REQ_GRANTED and are woken with resp() on the
//     IoSlave that received them (the per-core `resp_port`, set at dispatch),
//   * the per-core demux slave is muxed (built with its core id in build()).

#pragma once

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <string.h>
#include <optional>
#include "archi/eu_v3.h"

class Core_event_unit;
class Event_unit;
class Dispatch_unit;
class Mutex_unit;


// Timing constants

// Cycles required by the core to really wakeup after his clock is back.
#define EU_WAKEUP_REQ_LATENCY 6

// Cycles needed by the event unit to send back the clock once a core is woken.
#define EU_WAKEUP_LATENCY 2


// io_v2 helper: complete a request synchronously with the given response
// status. Replaces the legacy IO_REQ_OK / IO_REQ_INVALID return values.
static inline vp::IoReqStatus eu_io_resp(vp::IoReq *req, vp::IoRespStatus status)
{
    req->set_resp_status(status);
    return vp::IO_REQ_DONE;
}


class Soc_event_unit {
public:

  Soc_event_unit(Event_unit *top);
  void reset();

  int nb_fifo_events;
  int nb_free_events;
  int fifo_event_head;
  int fifo_event_tail;
  int *fifo_event;
  int fifo_soc_event;

  vp::WireSlave<int>     soc_event_itf;

  vp::IoReqStatus ioReq(vp::IoReq *req, uint32_t offset, bool is_write, uint32_t *data);

  void check_state();

private:
  static void sync(vp::Block *__this, int event);

  Event_unit *top;
  vp::Trace     trace;

};

class Mutex {
public:
  void reset();

  bool locked;
  uint32_t waiting_mask;
  uint32_t value;
  vp::IoReq *waiting_reqs[32];
};

class Mutex_unit {
public:
  Mutex_unit(Event_unit *top);

  void reset();

  Mutex *mutexes;
  vp::IoReqStatus req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data, int core);

private:
  vp::IoReqStatus enqueue_sleep(Mutex *mutex, vp::IoReq *req, int core_id) ;
  Event_unit *top;
  vp::Trace     trace;
  int nb_mutexes;
  int mutex_event;
};


class Dispatch {
public:
  uint32_t value;
  uint32_t status_mask;     // Cores that must get the value before it can be written again
  uint32_t config_mask;     // Cores that will get a valid value
  uint32_t waiting_mask;
  vp::IoReq *waiting_reqs[32];
};


class Dispatch_core
{
public:
  int tail;
};


class Dispatch_unit {
public:
  Dispatch_unit(Event_unit *top);

  vp::IoReqStatus req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data, int core);
  void reset();

  vp::IoReqStatus enqueue_sleep(Dispatch *dispatch, vp::IoReq *req, int core_id, bool is_caller=true);

  unsigned int config;
  int dispatch_event;
private:
  Event_unit *top;
  Dispatch_core *core;
  Dispatch *dispatches;
  int size;
  int fifo_head;
};


class Barrier {
public:
  uint32_t core_mask;
  uint32_t status;
  uint32_t target_mask;
};


class Barrier_unit {
public:
  Barrier_unit(Event_unit *top);

  vp::IoReqStatus req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data, int core);
  void reset();

private:
  void check_barrier(int barrier_id);

  Event_unit *top;
  vp::Trace     trace;
  Barrier *barriers;
  int nb_barriers;
  int barrier_event;
};


typedef enum
{
  CORE_STATE_NONE,
  CORE_STATE_WAITING_EVENT,
  CORE_STATE_WAITING_BARRIER,
  CORE_STATE_SKIP_ELW
} Event_unit_core_state_e;

class Event_unit : public vp::Component
{

  friend class Core_event_unit;
  friend class Dispatch_unit;
  friend class Barrier_unit;
  friend class Mutex_unit;
  friend class Soc_event_unit;

public:

  Event_unit(vp::ComponentConf &config);

  void reset(bool active);

  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
  static vp::IoReqStatus demux_req(vp::Block *__this, vp::IoReq *req, int core);
  static void irq_ack_sync(vp::Block *__this, int irq, int core);

protected:

  vp::Trace     trace;

  vp::IoSlave in{&Event_unit::req};

  Mutex_unit *mutex;
  Core_event_unit *core_eu;
  Dispatch_unit *dispatch;
  Barrier_unit *barrier_unit;
  Soc_event_unit *soc_event_unit;

  int nb_core;


  vp::IoReqStatus sw_events_req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data);
  void trigger_event(int event, uint32_t core_mask);
  void send_event(int core, uint32_t mask);
  static void in_event_sync(vp::Block *__this, bool active, int id);

};



class Core_event_unit
{
public:
  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
  void build(Event_unit *top, int core_id);
  void set_status(uint32_t new_value);
  void clear_status(uint32_t mask);
  void reset();
  void check_state();
  vp::IoReqStatus req(vp::IoReq *req, uint64_t offset, bool is_write, uint32_t *data);
  void check_wait_mask();
  void check_pending_req();
  void cancel_pending_req();
  vp::IoReqStatus wait_event(vp::IoReq *req, Event_unit_core_state_e wait_state=CORE_STATE_WAITING_EVENT);
  vp::IoReqStatus put_to_sleep(vp::IoReq *req, Event_unit_core_state_e wait_state=CORE_STATE_WAITING_EVENT);
  Event_unit_core_state_e get_state() { return state; }
  void set_state(Event_unit_core_state_e state) { this->state = state; }
  void irq_ack_sync(int irq, int core);
  static void wakeup_handler(vp::Block *__this, vp::ClockEvent *event);
  static void irq_wakeup_handler(vp::Block *__this, vp::ClockEvent *event);

  // Per-core demux slave port (muxed by core id). Built in build() once the
  // core id is known. The framework needs a stable pointer, so it is a
  // heap object rather than a by-value member (IoSlave has no default ctor).
  vp::IoSlave *demux_in;

  // IoSlave that received the currently-parked request, so the wake path can
  // resp() it back to the right master (the shared `in` or this core's
  // `demux_in`). Set by Event_unit::req / Event_unit::demux_req at dispatch.
  vp::IoSlave *resp_port = NULL;

  uint32_t status;
  uint32_t evt_mask;
  uint32_t irq_mask;
  uint32_t clear_evt_mask;

  int sync_irq;
  int pending_elw;

  // True when the parked request is a core WAIT register read, whose return
  // value (masked status) must be refreshed at wake time. Other parked
  // reads (dispatch, mutex, barrier) fill their own value.
  bool pending_req_is_wait;

private:
  Event_unit *top;
  int core_id;
  Event_unit_core_state_e state;
  vp::IoReq *pending_req;

  vp::WireMaster<bool> barrier_itf;
  vp::WireSlave<bool> in_event_itf[32];

  vp::WireMaster<bool>    clock_itf;

  vp::WireMaster<int>    irq_req_itf;
  vp::WireSlave<int>     irq_ack_itf;

  vp::ClockEvent *wakeup_event;
  vp::ClockEvent *irq_wakeup_event;

  // Owned by Event_unit (the Block); deferred-constructed in build() since
  // Core_event_unit instances are array-allocated before top is known.
  std::optional<vp::Register<bool>> is_active;

};
