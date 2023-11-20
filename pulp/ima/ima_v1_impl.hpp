/*
 * Copyright (C) 2020 ETH Zurich and University of Bologna
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
 * Authors: Nazareno Bruschi, Unibo (nazareno.bruschi@unibo.it)
 */

/* IMA main FSM States */
#define IMA_STATE_IDLE            0
#define IMA_STATE_ACQUIRE         1
#define IMA_STATE_P_WREQ          2
#define IMA_STATE_P_WR            3
#define IMA_STATE_J_REQ           4
#define IMA_STATE_J_EVAL          5
#define IMA_STATE_P_RREQ          6
#define IMA_STATE_P_RD            7

/* IMA eval FSM States */
#define IMA_EVAL_STATE_IDLE           0
#define IMA_EVAL_STATE_STREAM_IN      1
#define IMA_EVAL_STATE_COMPUTATION    2
#define IMA_EVAL_STATE_STREAM_OUT     3

/* Returns values of Acquire state */
#define IMA_ACQUIRE_LOCKED       -1
#define IMA_ACQUIRE_READY        0


class ima_job_t
{

public:

  /* Job parameters */
  int start_x;
  int start_y;
  int width;
  int height;
  /* Streamer parameters */
  int src_addr;
  int dest_addr;
  int stride_src;
  int stride_dest;
  int fetch_size;
  int store_size;
  int line_length;
  int feat_stride;
  int feat_length;
  int roll_length;
  int memt;
  int dw_mode;
  int first_data;
  /* Multi-jobs parameters */
  int jobs;
  int alpha_in_length;
  int alpha_in_stride;
  int beta_in_length;
  int beta_in_stride;
  int alpha_out_length;
  int alpha_out_stride;
  int beta_out_length;
  int beta_out_stride;
  /* Analog parameters */
  int adc_high;
  int adc_low;
  int analog_latency;

  int port;
  int latency;
};

class ima_plot_t
{

public:

  unsigned int pending_plot;
  vp::IoReq *pending_plot_req;

  int index_x;
  int index_y;

  int latency;
};

class ima_pw_t : public ima_plot_t
{

public:

  int start_x;
  int start_y;
  int width;
  int height;
};

class ima_pr_t : public ima_plot_t
{

public:

  int addr_x;
  int addr_y;
};


class ima_v1 : public vp::Component
{

public:

  ima_v1(vp::ComponentConf &config);

  void reset(bool active);

  static vp::IoReqStatus req(void *__this, vp::IoReq *req);
  static void job_handler(vp::Block *_this, vp::ClockEvent *event);
  static void plot_handler(vp::Block *_this, vp::ClockEvent *event);
  static void grant(void *_this, vp::IoReq *req);
  static void response(void *_this, vp::IoReq *req);
  void check_requests();

  void enqueue_write();
  void enqueue_read();
  void enqueue_job();

private:

  int nb_master_ports;
  int xbar_x;
  int xbar_y;
  int eval_time;
  int plot_write_time;
  int plot_read_time;

  bool stats;

  int state;
  int eval_state;

  void set_state(int new_state);
  void set_id(int id);

  void clear_ima();

  void exec_write_plot();
  void exec_read_plot();
  void exec_job();
  int8_t adc_clipping(float value);

  void job_update();
  void stream_reqs(bool is_write);
  int stream_access(int port, uint32_t addr, uint8_t *data, int size, bool is_write, int64_t *latency);
  int stream_update(int port, bool is_write);

  unsigned int *regs;
  int8_t *buffer_in;
  int8_t *buffer_out;
  int8_t **crossbar;

  ima_job_t *job;
  int remaining_jobs;

  ima_pw_t *pw_req;
  bool pending_write;

  ima_pr_t *pr_req;
  bool pending_read;

  vp::IoReqStatus ima_req(vp::IoReq *req, int new_state);
  vp::IoReqStatus trigger_req(vp::IoReq *req, int new_state);
  vp::IoReqStatus submit_req(vp::IoReq *req, int new_state);
  vp::IoReqStatus read_req(vp::IoReq *req, int new_state);
  vp::IoReqStatus acquire_req(vp::IoReq *req);

  void update_finished_jobs(int increment);

  vp::Trace trace;

  vp::IoSlave in;
  vp::IoMaster *out;
  vp::IoReq *reqs;
  int port_id;

  vp::WireMaster<bool> irq_0;
  vp::WireMaster<bool> irq_1;

  vp::ClockEvent *job_event;
  vp::ClockEvent *plot_event;
  vp::ClockEvent *stream_event;

  int pending_req;
  int enqueued_req;
  int remaining_in_req;
  int remaining_out_req;
  bool stalled;

  int extra_latency_in;

  int step_count;
  int feat_count;
  int roll_count;

  int line_fetch_lfover;
  int line_store_lfover;

  int alpha_in_count;
  int alpha_out_count;
  int beta_in_count;
  int beta_out_count;

  int count_stream_in;
  int count_stream_out;
  int count_compute;

};

static string get_state_name(int state) {
  switch (state) {
    case IMA_STATE_IDLE: return "idle";
    case IMA_STATE_ACQUIRE: return "acquire";
    case IMA_STATE_P_WREQ: return "wreq";
    case IMA_STATE_P_WR: return "write";
    case IMA_STATE_J_REQ: return "jreq";
    case IMA_STATE_J_EVAL: return "eval";
    case IMA_STATE_P_RREQ: return "rreq";
    case IMA_STATE_P_RD: return "read";
  }
  return "unknown";
}

static string get_eval_state_name(int state) {
  switch (state) {
    case IMA_EVAL_STATE_IDLE: return "idle";
    case IMA_EVAL_STATE_STREAM_IN: return "stream-in";
    case IMA_EVAL_STATE_COMPUTATION: return "compute";
    case IMA_EVAL_STATE_STREAM_OUT: return "stream-out";
  }
  return "unknown";
}
