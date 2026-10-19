// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//          Nazareno Bruschi, Unibo (nazareno.bruschi@unibo.it)

/*
 * uDMA v3 of pulp-open on io_v2, with its UART (v1) and HyperBus (v3)
 * peripherals.
 *
 * Port of the io (v1) model (pulp/udma/udma_v3_impl.*, uart/udma_uart_v1.cpp,
 * hyper/udma_hyper_v3.cpp), with the same channels, registers and peripheral
 * timings. What changes is the L2 side: the core has an L2 read port (TX
 * channels) and an L2 write port (RX channels), each an io_v2 single-request
 * master accepting the three answers of a target (done inline with a latency,
 * granted and answered later, denied and retried).
 */

#pragma once

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/uart.hpp>
#include <vp/itf/wire.hpp>
#include <vp/itf/hyper.hpp>
#include <vp/itf/clk.hpp>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <ips/pulp/udma/udma_v3/udma_v3_config.hpp>
#include <pulp/udma/archi/udma_v3.h>
#include <pulp/udma/archi/udma_hyper_v3.h>

class udma;
class Udma_channel;


/*
 * L2 access of the core: a word read for a TX channel or a write of an RX
 * channel. The request is the first field, so that a request coming back
 * from the L2 is the access.
 */
class Udma_l2_req
{
public:
  vp::IoReq req;
  uint8_t data[4];
  // Channel which issued the access
  Udma_channel *channel;
  // Number of valid bytes of a read word (the last word of a transfer may be
  // partial)
  int actual_size;
  // Cycle at which a read word reaches its channel
  int64_t ready_cycle;

  void set_next(Udma_l2_req *next) { this->next = next; }
  Udma_l2_req *get_next() { return next; }
  Udma_l2_req *next;
};


class Udma_transfer
{
public:
  uint32_t addr;
  uint32_t size;
  int transfer_size;
  int continuous_mode;
  uint32_t current_addr;
  int32_t remaining_size;
  uint64_t received_size;
  Udma_channel *channel;

  // Prepare the next L2 word access of the transfer, return true if it is
  // the last one
  bool prepare_req(Udma_l2_req *req);
  void set_next(Udma_transfer *next) { this->next = next; }
  Udma_transfer *get_next() { return next; }
  Udma_transfer *next;
};


template<class T>
class Udma_queue
{
public:
  Udma_queue(int size) : size(size) { init(); }
  void init() { first=NULL; last=NULL; nb_cmd=0; }
  T *pop();
  inline void push(T *cmd);
  // Insert ordered by ready cycle, after the ones with the same cycle
  void push_ordered(T *cmd);
  bool is_full() { return nb_cmd >= size; }
  bool is_empty() { return nb_cmd == 0; }
  T *get_first() { return first; }

private:
  T *first;    // First command of the queue, commands are popped from here
  T *last;     // Last command of the queue, commands are pushed here
  int        nb_cmd;
  int        size;
};


class Udma_channel
{
public:
  Udma_channel(udma *top, int id, std::string name);
  virtual vp::IoRespStatus req(vp::IoReq *req, uint64_t offset);
  virtual void reset(bool active);
  virtual bool is_tx() { return false; }
  void set_next(Udma_channel *next) { this->next = next; }
  Udma_channel *get_next() { return next; }
  void event_handler();
  bool prepare_req(Udma_l2_req *req);
  void push_ready_req(Udma_l2_req *req);
  bool handle_ready_req_end(Udma_l2_req *req);
  virtual bool is_busy() { return false; }
  virtual void handle_ready() { }
  virtual void handle_ready_reqs();
  virtual void handle_transfer_end();
  void check_state();

  Udma_transfer *current_cmd;

  void build_reqs_and_enqueue(Udma_transfer *current_req);

protected:
  vp::Trace     trace;
  Udma_queue<Udma_l2_req> *ready_reqs;
  udma *top;

private:
  virtual vp::IoRespStatus saddr_req(vp::IoReq *req);
  virtual vp::IoRespStatus size_req(vp::IoReq *req);
  virtual vp::IoRespStatus cfg_req(vp::IoReq *req);
  void enqueue_transfer();
  virtual void handle_ready_req(Udma_l2_req *req);

  uint32_t saddr;
  uint32_t size;

  int transfer_size;
  bool continuous_mode;

  int id;
  std::string name;
  Udma_channel *next;

  vp::ClockEvent *event;

  Udma_queue<Udma_transfer> *free_reqs;
  Udma_queue<Udma_transfer> *pending_reqs;

  vp::Trace     state_event;
};


class Udma_rx_channel : public Udma_channel
{
public:
  Udma_rx_channel(udma *top, int id, std::string name) : Udma_channel(top, id, name) {}
  bool is_tx() { return false; }
  void reset(bool active);
  void push_data(uint8_t *data, int size);
  bool has_cmd() { return this->current_cmd != NULL; }

private:
  int pending_byte_index;
  uint32_t pending_word;
};


class Udma_tx_channel : public Udma_channel
{
public:
  Udma_tx_channel(udma *top, int id, std::string name) : Udma_channel(top, id, name) {}
  bool is_tx() { return true; }
};


class Udma_periph
{
public:
  Udma_periph(udma *top, int id);
  virtual vp::IoRespStatus req(vp::IoReq *req, uint64_t offset);
  virtual void reset(bool active);
  void clock_gate(bool is_on);

  int id;

  bool get_periph_status() { return is_on; }

protected:
  Udma_channel *channel0 = NULL;
  Udma_channel *channel1 = NULL;
  udma *top;

private:
  virtual vp::IoRespStatus custom_req(vp::IoReq *req, uint64_t offset);
  bool is_on;
};


/*
 * UART
 */

class Uart_periph_v1;

typedef enum
{
  UART_RX_STATE_WAIT_START,
  UART_RX_STATE_DATA,
  UART_RX_STATE_PARITY,
  UART_RX_STATE_WAIT_STOP
} uart_rx_state_e;

typedef enum
{
  UART_TX_STATE_START,
  UART_TX_STATE_DATA,
  UART_TX_STATE_PARITY,
  UART_TX_STATE_STOP
} uart_tx_state_e;


class Uart_rx_channel : public Udma_rx_channel
{
public:
  Uart_rx_channel(udma *top, Uart_periph_v1 *periph, int id, std::string name);
  void handle_rx_bit(int bit);
  bool is_busy();

private:
  void reset(bool active);

  Uart_periph_v1 *periph;

  uart_rx_state_e state;
  int parity;
  int stop_bits;
  int nb_received_bits;
  uint8_t pending_rx_byte;
};


class Uart_tx_channel : public Udma_tx_channel
{
public:
  Uart_tx_channel(udma *top, Uart_periph_v1 *periph, int id, std::string name);
  void handle_ready_reqs();
  bool is_busy();

private:
  void reset(bool active);
  void check_state();
  static void handle_pending_word(vp::Block *__this, vp::ClockEvent *event);

  Uart_periph_v1 *periph;

  vp::ClockEvent *pending_word_event;

  uart_tx_state_e state;
  uint32_t pending_word;
  int pending_bits;
  int sent_bits;
  int stop_bits;
  int parity;
  Udma_l2_req *pending_req;
  int64_t next_bit_cycle;
};


class Uart_periph_v1 : public Udma_periph
{
  friend class Uart_tx_channel;
  friend class Uart_rx_channel;

public:
  Uart_periph_v1(udma *top, int id, int itf_id);
  vp::IoRespStatus custom_req(vp::IoReq *req, uint64_t offset);
  void reset(bool active);

protected:
  vp::UartMaster uart_itf;

private:
  void set_setup_reg(uint32_t value);
  vp::IoRespStatus status_req(vp::IoReq *req);
  vp::IoRespStatus setup_req(vp::IoReq *req);
  static void rx_sync(vp::Block *__this, int data);

  vp::Trace     trace;

  uint32_t setup_reg_value;
  int parity;
  int bit_length;
  int stop_bits;
  int tx;
  int rx;
  int clkdiv;
  int rx_pe;
};


/*
 * HYPER
 */

class Hyper_periph_v3;

class Hyper_transfer : public Udma_transfer
{
public:
  uint32_t ex_addr;
  int mem_sel;
  int ca_setup;
  int data;
  int latency;
  int en_add_latency;
  bool cfg_setup;
  int channel_id;
  bool is_write;
  bool twd_act;
  int twd_count;
  int twd_stride;
  bool twd_is_l2;

  void set_next(Hyper_transfer *next) { this->next = next; }
  Hyper_transfer *get_next() { return next; }
  Hyper_transfer *next;
};


class Hyper_v3_rx_channel : public Udma_rx_channel
{
  friend class Hyper_periph_v3;

public:
  Hyper_v3_rx_channel(udma *top, Hyper_periph_v3 *periph, int id, std::string name);
  void handle_rx_data(int data);
  void handle_ready();

private:
  void reset(bool active);
  Hyper_periph_v3 *periph;
};


class Hyper_v3_tx_channel : public Udma_tx_channel
{
  friend class Hyper_periph_v3;

public:
  Hyper_v3_tx_channel(udma *top, Hyper_periph_v3 *periph, int id, std::string name);

protected:
  void handle_ready_reqs();

private:
  void reset(bool active);
  Hyper_periph_v3 *periph;
};


typedef enum
{
  HYPER_STATE_IDLE,
  HYPER_STATE_CS,
  HYPER_STATE_CA,
  HYPER_STATE_DATA,
  HYPER_STATE_CS_OFF,
} hyper_state_e;


class Hyper_periph_v3 : public Udma_periph
{
  friend class Hyper_v3_tx_channel;
  friend class Hyper_v3_rx_channel;

public:
  Hyper_periph_v3(udma *top, int id, int itf_id);
  vp::IoRespStatus req(vp::IoReq *req, uint64_t offset);
  void reset(bool active);
  static void rx_sync(vp::Block *__this, int data);
  static void handle_pending_word(vp::Block *__this, vp::ClockEvent *event);
  void check_state();
  void handle_ready_reqs();

private:
  bool get_cfg_status(int id) { return (cfg_setup[id] & command_word[id]); }
  int update_trans_id_alloc();
  int transfer_arbiter(int id);
  void fetch_from_fifos();
  void transfer_splitter();
  void set_device(int cs);
  void set_busy_reg(int id, int value);
  int get_busy_reg(int id);
  void update_nb_tran(int id, int value);
  int get_nb_tran(int id);
  void enqueue_transfer(int id);
  vp::IoRespStatus custom_req(vp::IoReq *req, uint64_t offset, int id);
  vp::IoRespStatus hyper_cfg_req(vp::IoReq *req, int id);
  vp::IoRespStatus access_common_regs(vp::IoReq *req, uint64_t offset, int id);
  vp::IoRespStatus access_private_regs(vp::IoReq *req, uint64_t offset, int id);

  vp::HyperMaster hyper_itf;
  unsigned int regs[HYPER_NB_CHANNELS][HYPER_NB_CONFIG_REGS];
  unsigned int common_regs[HYPER_NB_COMMON_REGS];
  int clkdiv;
  Hyper_v3_tx_channel *tx_channel;
  Hyper_v3_rx_channel *rx_channel;

  vp::Trace     trace;

  int pending_bytes;
  vp::ClockEvent *pending_word_event;
  int64_t next_bit_cycle;
  Udma_l2_req *pending_req;
  uint32_t pending_word;
  int transfer_size;
  hyper_state_e state;
  int delay;
  int ca_count;
  bool pending_tx;
  bool pending_rx;

  union
  {
    struct {
      unsigned int low_addr:3;
      unsigned int reserved:13;
      unsigned int high_addr:29;
      unsigned int burst_type:1;
      unsigned int address_space:1;
      unsigned int read:1;
    } __attribute__((packed));
    uint8_t raw[6];
  } ca;

  bool cfg_setup[HYPER_NB_CHANNELS];
  bool command_word[HYPER_NB_CHANNELS];
  bool ending;
  bool command_mode;
  int twd_count;
  bool write_trans;
  bool continuous_mode;
  int transfer_size_mode;
  Udma_queue<Hyper_transfer> *free_fifo[HYPER_NB_CHANNELS];
  Udma_queue<Hyper_transfer> *pending_fifo[HYPER_NB_CHANNELS];
  Hyper_transfer *current_command;
  int channel_id;
  int mem_sel;
};


/*
 * UDMA
 */

template<class T>
inline void Udma_queue<T>::push(T *cmd)
{
  if (first)
    last->set_next(cmd);
  else
    first = cmd;
  cmd->set_next(NULL);
  last = cmd;
  nb_cmd++;
}


template<class T>
T *Udma_queue<T>::pop()
{
  if (!first) return NULL;
  T *cmd = first;
  first = cmd->get_next();
  nb_cmd--;

  return cmd;
}


template<class T>
void Udma_queue<T>::push_ordered(T *cmd)
{
  T *current = first, *prev = NULL;
  while (current && cmd->ready_cycle >= current->ready_cycle)
  {
    prev = current;
    current = current->get_next();
  }

  if (current == NULL)
    last = cmd;

  if (prev)
    prev->set_next(cmd);
  else
    first = cmd;
  cmd->set_next(current);
  nb_cmd++;
}


class udma : public vp::Component
{
  friend class Udma_periph;
  friend class Udma_rx_channel;

public:
  udma(vp::ComponentConf &config);

  void reset(bool active);

  void enqueue_ready(Udma_channel *channel);

  static void channel_handler(vp::Block *__this, vp::ClockEvent *event);
  void free_read_req(Udma_l2_req *req);

  void trigger_event(int event);

  vp::Trace *get_trace() { return &this->trace; }
  vp::ClockEngine *get_periph_clock() { return this->periph_clock; }

  // Write of an RX channel to the L2
  Udma_l2_req *get_write_req();
  void push_l2_write_req(Udma_l2_req *req);

private:
  void check_state();
  // Send the pending L2 writes and reads, one of each per cycle
  void send_l2_write();
  void send_l2_read();
  // The answer of an L2 read: the word reaches its channel `latency` cycles
  // from now
  void l2_read_end(Udma_l2_req *req, int64_t latency);
  void free_write_req(Udma_l2_req *req);

  vp::IoRespStatus conf_req(vp::IoReq *req, uint64_t offset);
  vp::IoRespStatus periph_req(vp::IoReq *req, uint64_t offset);
  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
  static void event_handler(vp::Block *__this, vp::ClockEvent *event);
  static void l2_ro_retry(vp::Block *__this, vp::IoRetryChannel channel);
  static vp::IoRespAck l2_ro_resp(vp::Block *__this, vp::IoReq *req);
  static void l2_wo_retry(vp::Block *__this, vp::IoRetryChannel channel);
  static vp::IoRespAck l2_wo_resp(vp::Block *__this, vp::IoReq *req);
  static void clk_reg(vp::Component *_this, vp::Component *clock);

  UdmaV3Config cfg;

  vp::Trace     trace;
  vp::IoSlave in{&udma::req};
  vp::IoMaster l2_ro{&udma::l2_ro_retry, &udma::l2_ro_resp};
  vp::IoMaster l2_wo{&udma::l2_wo_retry, &udma::l2_wo_resp};
  vp::ClkSlave    periph_clock_itf;
  vp::ClockEngine *periph_clock;

  std::vector<Udma_periph *>periphs;
  Udma_queue<Udma_channel> *ready_rx_channels;
  Udma_queue<Udma_channel> *ready_tx_channels;
  uint32_t clock_gating;
  vp::ClockEvent *event;

  // Free read accesses (the read FIFO), and the ones waiting for their word
  // to reach its channel
  Udma_queue<Udma_l2_req> *l2_read_reqs;
  Udma_queue<Udma_l2_req> *l2_read_waiting_reqs;
  // Read denied by the L2, sent again on its retry
  Udma_l2_req *l2_read_denied;

  // Writes waiting to be sent, and the free ones
  Udma_queue<Udma_l2_req> *l2_write_reqs;
  Udma_queue<Udma_l2_req> *l2_write_free;
  // Write denied by the L2, sent again on its retry
  Udma_l2_req *l2_write_denied;

  vp::WireMaster<int>    event_itf;
};
