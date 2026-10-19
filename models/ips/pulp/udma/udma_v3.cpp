// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <pulp/udma/archi/utils.h>
#include "udma_v3.hpp"


void Udma_rx_channel::push_data(uint8_t *data, int size)
{
  if (current_cmd == NULL)
  {
    return;
  }

  if (size + this->pending_byte_index > 4)
  {
    trace.force_warning("Trying to push more than 4 bytes from peripheral to udma core\n");
    return;
  }

  memcpy(&(((uint8_t *)&this->pending_word)[this->pending_byte_index]), data, size);

  this->pending_byte_index += size;

  if (this->pending_byte_index >= 4 || this->pending_byte_index >= current_cmd->remaining_size)
  {
    this->pending_byte_index = 0;
    Udma_l2_req *req = this->top->get_write_req();
    memcpy(req->data, &this->pending_word, 4);
    bool end = current_cmd->prepare_req(req);
    // Only the valid bytes of the last word are written
    req->req.set_size(req->actual_size);
    trace.msg("Writing %d bytes to memory (value: 0x%x, addr: 0x%x)\n", req->actual_size,
      this->pending_word, req->req.get_addr());
    this->top->push_l2_write_req(req);
    if (end)
    {
      handle_transfer_end();
    }
  }
}

void Udma_rx_channel::reset(bool active)
{
  Udma_channel::reset(active);

  if (active)
  {
    pending_byte_index = 0;
  }
}


void Udma_channel::handle_transfer_end()
{
  trace.msg("Current transfer is finished\n");
  free_reqs->push(current_cmd);
  current_cmd = NULL;
  top->trigger_event(id);
  this->check_state();
}

void Udma_channel::handle_ready_reqs()
{
  if (!ready_reqs->is_empty())
  {
    Udma_l2_req *req = ready_reqs->pop();
    handle_ready_req_end(req);
  }
}

void Udma_channel::handle_ready_req(Udma_l2_req *req)
{
  ready_reqs->push(req);
  handle_ready_reqs();
}

bool Udma_channel::handle_ready_req_end(Udma_l2_req *req)
{
  bool end = false;
  if (current_cmd && current_cmd->received_size >= current_cmd->size)
  {
    handle_transfer_end();
    end = true;
  }
  top->free_read_req(req);
  return end;
}

void Udma_channel::push_ready_req(Udma_l2_req *req)
{
  current_cmd->received_size += req->req.get_size();

  trace.msg("Received data from L2 (cmd: %p, data_size: 0x%x, transfer_size: 0x%x, received_size: 0x%x, value: 0x%x)\n",
    current_cmd, req->req.get_size(), current_cmd->size, current_cmd->received_size, *(uint32_t *)req->data);

  handle_ready_req(req);
}

vp::IoRespStatus Udma_channel::saddr_req(vp::IoReq *req)
{
  uint32_t *data = (uint32_t *)req->get_data();
  if (req->get_is_write())
  {
    trace.msg("Setting saddr register (value: 0x%x)\n", *data);
    saddr = *data;
  }
  else
  {
    *data = saddr;
  }
  return vp::IO_RESP_OK;
}


vp::IoRespStatus Udma_channel::size_req(vp::IoReq *req)
{
  uint32_t *data = (uint32_t *)req->get_data();
  if (req->get_is_write())
  {
    trace.msg("Setting size register (value: 0x%x)\n", *data);
    size = *(int32_t *)data;
  }
  else
  {
    *data = size;
  }
  return vp::IO_RESP_OK;
}


void Udma_channel::event_handler()
{
  if (!pending_reqs->is_empty() && current_cmd == NULL)
  {
    current_cmd = pending_reqs->pop();
    trace.msg("New ready transfer (cmd: %p)\n", current_cmd);
    top->enqueue_ready(this);
  }
}

void Udma_channel::check_state()
{
  if (!pending_reqs->is_empty() && current_cmd == NULL)
  {
    top->event_enqueue_ext(event, 1);
  }

  if (free_reqs->is_full())
  {
    this->state_event.event_highz();
  }
  else
  {
    uint8_t one = 1;
    this->state_event.event(&one);
  }
}


void Udma_channel::build_reqs_and_enqueue(Udma_transfer *req)
{
  saddr = req->addr;
  size = req->size;
  transfer_size = req->transfer_size;
  continuous_mode = req->continuous_mode;

  enqueue_transfer();
}


void Udma_channel::enqueue_transfer()
{
  if (free_reqs->is_empty())
  {
    vp_warning_always(&this->trace, "Trying to enqueue transfer while already 2 are pending\n");
    return;
  }

  Udma_transfer *req = free_reqs->pop();

  trace.msg("Enqueueing new transfer (req: %p, addr: 0x%x, size: 0x%x, transfer_size: %s, continuous: %d)\n",
    req, saddr, size, transfer_size == 0 ? "8bits" : transfer_size == 1 ? "16bits" : "32bits",
    continuous_mode);

  req->addr = saddr;
  req->current_addr = saddr;
  req->size = size;
  req->remaining_size = size;
  req->transfer_size = transfer_size;
  req->received_size = 0;
  req->continuous_mode = continuous_mode;
  req->channel = this;
  pending_reqs->push(req);

  check_state();
}


vp::IoRespStatus Udma_channel::cfg_req(vp::IoReq *req)
{
  uint32_t *data = (uint32_t *)req->get_data();
  if (req->get_is_write())
  {
    continuous_mode = ARCHI_REG_FIELD_GET(*data, UDMA_CHANNEL_CFG_CONT_BIT, 1);
    transfer_size = ARCHI_REG_FIELD_GET(*data, UDMA_CHANNEL_CFG_SIZE_BIT, 1);
    bool channel_enabled = ARCHI_REG_FIELD_GET(*data, UDMA_CHANNEL_CFG_EN_BIT, 1);
    bool channel_clear = ARCHI_REG_FIELD_GET(*data, UDMA_CHANNEL_CFG_CLEAR_BIT, 1);
    trace.msg("Setting cfg register (continuous: %d, size: %s, enable: %d, clear: %d)\n",
      continuous_mode, transfer_size == 0 ? "8bits" : transfer_size == 1 ? "16bits" : "32bits",
      channel_enabled, channel_clear);

    if (channel_clear)
    {
      trace.msg("UNIMPLEMENTED AT %s %d\n", __FILE__, __LINE__);
      return vp::IO_RESP_INVALID;
    }

    if (channel_enabled)
    {
      enqueue_transfer();
    }
  }
  else
  {
    *data = (continuous_mode << UDMA_CHANNEL_CFG_CONT_BIT) |
      (transfer_size << UDMA_CHANNEL_CFG_SIZE_BIT) |
      (!free_reqs->is_full() << UDMA_CHANNEL_CFG_EN_BIT) |
      (free_reqs->is_empty() << UDMA_CHANNEL_CFG_SHADOW_BIT);
  }
  return vp::IO_RESP_OK;
}


vp::IoRespStatus Udma_channel::req(vp::IoReq *req, uint64_t offset)
{
  if (offset == UDMA_CHANNEL_SADDR_OFFSET)
  {
    return saddr_req(req);
  }
  else if (offset == UDMA_CHANNEL_SIZE_OFFSET)
  {
    return size_req(req);
  }
  else if (offset == UDMA_CHANNEL_CFG_OFFSET)
  {
    return cfg_req(req);
  }
  return vp::IO_RESP_INVALID;
}


Udma_channel::Udma_channel(udma *top, int id, std::string name) : top(top), id(id), name(name)
{
  top->traces.new_trace(name + "/trace", &trace, vp::DEBUG);

  // Each channel can handle 2 transfers at the same time
  free_reqs = new Udma_queue<Udma_transfer>(2);
  pending_reqs = new Udma_queue<Udma_transfer>(2);
  ready_reqs = new Udma_queue<Udma_l2_req>(-1);
  free_reqs->push( new Udma_transfer());
  free_reqs->push( new Udma_transfer());

  event = top->event_new((vp::Block *)this, udma::channel_handler);

  top->traces.new_trace_event(name + "/state", &this->state_event, 8);
}


bool Udma_channel::prepare_req(Udma_l2_req *req)
{
  return current_cmd->prepare_req(req);
}


void Udma_channel::reset(bool active)
{
  if (active)
  {
    current_cmd = NULL;
    continuous_mode = 0;
    transfer_size = 0;
    this->state_event.event_highz();
  }
}


Udma_periph::Udma_periph(udma *top, int id) : id(id), top(top)
{
}


void Udma_periph::clock_gate(bool new_is_on)
{
  if (is_on != new_is_on)
  {
    if (new_is_on)
      top->trace.msg("Activating periph (periph: %d)\n", id);
    else
      top->trace.msg("Dectivating periph (periph: %d)\n", id);
  }
  is_on = new_is_on;
}


void Udma_periph::reset(bool active)
{
  if (active)
  {
    is_on = false;
  }

  if (channel0)
    channel0->reset(active);
  if (channel1)
    channel1->reset(active);
}


vp::IoRespStatus Udma_periph::custom_req(vp::IoReq *req, uint64_t offset)
{
  return vp::IO_RESP_INVALID;
}


vp::IoRespStatus Udma_periph::req(vp::IoReq *req, uint64_t offset)
{
  if (!is_on)
  {
    // As the io (v1) model, accesses to a gated peripheral are ignored
    return vp::IO_RESP_OK;
  }

  if (offset < UDMA_CHANNEL_TX_OFFSET)
  {
    if (channel0 == NULL)
    {
      top->trace.force_warning("Trying to access non-existing RX channel\n");
      return vp::IO_RESP_INVALID;
    }
    return channel0->req(req, offset);
  }
  else if (offset < UDMA_CHANNEL_CUSTOM_OFFSET)
  {
    if (channel1 == NULL)
    {
      top->trace.force_warning("Trying to access non-existing TX channel\n");
      return vp::IO_RESP_INVALID;
    }
    return channel1->req(req, offset - UDMA_CHANNEL_TX_OFFSET);
  }
  else
  {
    return custom_req(req, offset - UDMA_CHANNEL_CUSTOM_OFFSET);
  }
}


// The uDMA cannot do misaligned accesses: it uses strobes not to overwrite the
// memory around. The model just issues the access at the current address.
bool Udma_transfer::prepare_req(Udma_l2_req *req)
{
  req->req.prepare();
  req->req.set_addr(current_addr);
  req->req.set_size(4);
  req->req.set_data(req->data);
  req->channel = channel;
  req->actual_size = remaining_size > 4 ? 4 : remaining_size;

  current_addr += 4;
  remaining_size -= 4;

  return remaining_size <= 0;
}


void udma::trigger_event(int event)
{
  trace.msg("Triggering event (event: %d)\n", event);
  event_itf.sync(event);
}


udma::udma(vp::ComponentConf &config)
: vp::Component(config, this->cfg)
{
  traces.new_trace("trace", &trace, vp::DEBUG);

  new_slave_port("input", &in);

  this->periph_clock_itf.set_reg_meth(&udma::clk_reg);
  new_slave_port("periph_clock", &this->periph_clock_itf);

  periphs.resize(this->cfg.nb_periphs);

  new_master_port("l2_ro", &l2_ro);
  new_master_port("l2_wo", &l2_wo);

  new_master_port("event_itf", &event_itf);

  event = event_new(udma::event_handler);

  int l2_read_fifo_size = this->cfg.l2_read_fifo_size;
  l2_read_reqs = new Udma_queue<Udma_l2_req>(l2_read_fifo_size);
  l2_read_waiting_reqs = new Udma_queue<Udma_l2_req>(l2_read_fifo_size);
  for (int i=0; i<l2_read_fifo_size; i++)
  {
    Udma_l2_req *req = new Udma_l2_req();
    req->req.set_is_write(false);
    l2_read_reqs->push(req);
  }
  l2_write_reqs = new Udma_queue<Udma_l2_req>(-1);
  l2_write_free = new Udma_queue<Udma_l2_req>(-1);

  ready_rx_channels = new Udma_queue<Udma_channel>(this->cfg.nb_periphs);
  ready_tx_channels = new Udma_queue<Udma_channel>(this->cfg.nb_periphs);

  for (int i=0; i<this->cfg.nb_periphs; i++)
    periphs[i] = NULL;

  for (int i=0; i<this->cfg.nb_uart; i++)
  {
    int id = this->cfg.uart_id + i;
    trace.msg("Instantiating UART channel (id: %d)\n", id);
    periphs[id] = new Uart_periph_v1(this, id, i);
  }

  for (int i=0; i<this->cfg.nb_hyper; i++)
  {
    int id = this->cfg.hyper_id + i * (HYPER_NB_CHANNELS + 1);
    trace.msg("Instantiating HYPER channel (id: %d)\n", id);
    Hyper_periph_v3 *periph = new Hyper_periph_v3(this, id, i);
    // The peripheral spans the areas of its transfer ids and of its common
    // registers
    for (int channel = 0; channel <= HYPER_NB_CHANNELS; channel++)
    {
      periphs[id + channel] = periph;
    }
  }
}


Udma_l2_req *udma::get_write_req()
{
  Udma_l2_req *req = this->l2_write_free->pop();
  if (req == NULL)
  {
    req = new Udma_l2_req();
  }
  req->req.set_is_write(true);
  return req;
}


void udma::free_write_req(Udma_l2_req *req)
{
  this->l2_write_free->push(req);
}


void udma::push_l2_write_req(Udma_l2_req *req)
{
  this->l2_write_reqs->push(req);
  this->check_state();
}


void udma::channel_handler(vp::Block *__this, vp::ClockEvent *event)
{
  Udma_channel *channel = (Udma_channel *)__this;
  channel->event_handler();
}


void udma::enqueue_ready(Udma_channel *channel)
{
  if (channel->is_tx())
    ready_tx_channels->push(channel);
  else
    channel->handle_ready();

  check_state();
}


void udma::send_l2_write()
{
  if (this->l2_write_denied != NULL || this->l2_write_reqs->is_empty())
  {
    return;
  }

  Udma_l2_req *req = this->l2_write_reqs->pop();
  this->trace.msg("Sending write request to L2 (value: 0x%x, addr: 0x%x, size: 0x%x)\n",
    *(uint32_t *)req->data, req->req.get_addr(), req->req.get_size());

  vp::IoReqStatus status = this->l2_wo.req(&req->req);
  if (status == vp::IO_REQ_DONE)
  {
    if (req->req.get_resp_status() != vp::IO_RESP_OK)
    {
      this->trace.force_warning("Invalid L2 write (addr: 0x%x)\n", req->req.get_addr());
    }
    this->free_write_req(req);
  }
  else if (status == vp::IO_REQ_DENIED)
  {
    this->l2_write_denied = req;
  }
  // Granted: freed when its response comes back
}


void udma::send_l2_read()
{
  if (this->l2_read_denied != NULL || this->ready_tx_channels->is_empty() ||
    this->l2_read_reqs->is_empty())
  {
    return;
  }

  Udma_l2_req *req = this->l2_read_reqs->pop();
  Udma_channel *channel = this->ready_tx_channels->pop();
  if (!channel->prepare_req(req))
  {
    this->ready_tx_channels->push(channel);
  }

  this->trace.msg("Sending read request to L2 (addr: 0x%x, size: 0x%x)\n",
    req->req.get_addr(), req->req.get_size());

  vp::IoReqStatus status = this->l2_ro.req(&req->req);
  if (status == vp::IO_REQ_DONE)
  {
    this->l2_read_end(req, req->req.get_full_latency() + 1);
  }
  else if (status == vp::IO_REQ_DENIED)
  {
    this->l2_read_denied = req;
  }
  // Granted: goes on when its response comes back
}


void udma::l2_read_end(Udma_l2_req *req, int64_t latency)
{
  if (req->req.get_resp_status() != vp::IO_RESP_OK)
  {
    this->trace.force_warning("Invalid L2 read (addr: 0x%x)\n", req->req.get_addr());
  }
  this->trace.msg("Read FIFO received word from L2 (value: 0x%x)\n", *(uint32_t *)req->data);
  req->ready_cycle = this->clock.get_cycles() + latency;
  this->l2_read_waiting_reqs->push_ordered(req);
}


void udma::event_handler(vp::Block *__this, vp::ClockEvent *event)
{
  udma *_this = (udma *)__this;

  _this->send_l2_write();
  _this->send_l2_read();

  Udma_l2_req *req = _this->l2_read_waiting_reqs->get_first();
  while (req != NULL && req->ready_cycle <= _this->clock.get_cycles())
  {
    _this->trace.msg("Read request is ready, pushing to channel (req: %p)\n", req);

    _this->l2_read_waiting_reqs->pop();
    req->channel->push_ready_req(req);

    req = _this->l2_read_waiting_reqs->get_first();
  }

  _this->check_state();
}


void udma::l2_ro_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
  udma *_this = (udma *)__this;
  Udma_l2_req *req = _this->l2_read_denied;
  if (req == NULL)
  {
    return;
  }

  // Sent again right away, inside the retry
  _this->l2_read_denied = NULL;
  vp::IoReqStatus status = _this->l2_ro.req(&req->req);
  if (status == vp::IO_REQ_DONE)
  {
    _this->l2_read_end(req, req->req.get_full_latency() + 1);
  }
  else if (status == vp::IO_REQ_DENIED)
  {
    _this->l2_read_denied = req;
  }
  _this->check_state();
}


vp::IoRespAck udma::l2_ro_resp(vp::Block *__this, vp::IoReq *req)
{
  udma *_this = (udma *)__this;
  _this->l2_read_end((Udma_l2_req *)req, req->get_full_latency() + 1);
  _this->check_state();
  return vp::IO_RESP_ACCEPTED;
}


void udma::l2_wo_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
  udma *_this = (udma *)__this;
  Udma_l2_req *req = _this->l2_write_denied;
  if (req == NULL)
  {
    return;
  }

  // Sent again right away, inside the retry
  _this->l2_write_denied = NULL;
  vp::IoReqStatus status = _this->l2_wo.req(&req->req);
  if (status == vp::IO_REQ_DONE)
  {
    _this->free_write_req(req);
  }
  else if (status == vp::IO_REQ_DENIED)
  {
    _this->l2_write_denied = req;
  }
  _this->check_state();
}


vp::IoRespAck udma::l2_wo_resp(vp::Block *__this, vp::IoReq *req)
{
  udma *_this = (udma *)__this;
  if (req->get_resp_status() != vp::IO_RESP_OK)
  {
    _this->trace.force_warning("Invalid L2 write (addr: 0x%x)\n", req->get_addr());
  }
  _this->free_write_req((Udma_l2_req *)req);
  _this->check_state();
  return vp::IO_RESP_ACCEPTED;
}


void udma::free_read_req(Udma_l2_req *req)
{
  l2_read_reqs->push(req);
  check_state();
}


void udma::check_state()
{
  if ((this->l2_read_denied == NULL && !ready_tx_channels->is_empty() && !l2_read_reqs->is_empty()) ||
    (this->l2_write_denied == NULL && !l2_write_reqs->is_empty()))
  {
    event_reenqueue_ext(event, 1);
  }

  if (!l2_read_waiting_reqs->is_empty())
  {
    int64_t latency = l2_read_waiting_reqs->get_first()->ready_cycle - clock.get_cycles();
    event_reenqueue_ext(event, latency > 0 ? latency : 1);
  }
}


vp::IoRespStatus udma::conf_req(vp::IoReq *req, uint64_t offset)
{
  uint32_t *data = (uint32_t *)req->get_data();
  bool is_write = req->get_is_write();

  if (offset == UDMA_CONF_CG_OFFSET)
  {
    if (is_write)
    {
      trace.msg("Writing clock-enable register (current_value: 0x%x, new_value: 0x%x)\n",
        clock_gating, *data);
      clock_gating = *data;
      for (int i=0; i<this->cfg.nb_periphs; i++)
      {
        if (periphs[i] != NULL && periphs[i]->id == i)
          periphs[i]->clock_gate((clock_gating >> i) & 1);
      }
    }
    else
      *data = clock_gating;

    return vp::IO_RESP_OK;
  }
  else if (offset == UDMA_CONF_EVTIN_OFFSET)
  {
    trace.msg("Unimplemented at %s %d\n", __FILE__, __LINE__);
  }

  return vp::IO_RESP_INVALID;
}


vp::IoRespStatus udma::periph_req(vp::IoReq *req, uint64_t offset)
{
  uint64_t size = req->get_size();

  if (size != 4) return vp::IO_RESP_INVALID;

  int periph_id = UDMA_PERIPH_GET(offset);

  if (periph_id >= this->cfg.nb_periphs || periphs[periph_id] == NULL)
  {
    trace.force_warning("Accessing invalid periph (id: %d)\n", periph_id);
    return vp::IO_RESP_INVALID;
  }

  int real_id = periphs[periph_id]->id;

  return periphs[periph_id]->req(req, offset - (UDMA_PERIPH_OFFSET(real_id) - UDMA_FIRST_CHANNEL_OFFSET));
}


vp::IoReqStatus udma::req(vp::Block *__this, vp::IoReq *req)
{
  udma *_this = (udma *)__this;

  uint64_t offset = req->get_addr();
  uint64_t size = req->get_size();
  bool is_write = req->get_is_write();

  _this->trace.msg("UDMA access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

  vp::IoRespStatus status;
  if (offset < UDMA_FIRST_CHANNEL_OFFSET)
  {
    status = _this->conf_req(req, offset - UDMA_CONF_OFFSET);
  }
  else
  {
    status = _this->periph_req(req, offset - UDMA_FIRST_CHANNEL_OFFSET);
  }

  req->set_resp_status(status);
  return vp::IO_REQ_DONE;
}


void udma::clk_reg(vp::Component *__this, vp::Component *clock)
{
  udma *_this = (udma *)__this;
  _this->periph_clock = (vp::ClockEngine *)clock;
}


void udma::reset(bool active)
{
  if (active)
  {
    clock_gating = 0;
    this->l2_read_denied = NULL;
    this->l2_write_denied = NULL;
  }

  for (int i=0; i<this->cfg.nb_periphs; i++)
  {
    if (periphs[i] != NULL && periphs[i]->id == i)
      periphs[i]->reset(active);
  }
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new udma(config);
}
