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

/* REDMULE main FSM States */
#define REDMULE_STATE_IDLE            0
#define REDMULE_STATE_ACQUIRE         1
#define REDMULE_STATE_J_EVAL          2

/* REDMULE eval FSM States */
#define REDMULE_EVAL_STATE_IDLE           0
#define REDMULE_EVAL_STATE_STREAM_IN_X    1
#define REDMULE_EVAL_STATE_STREAM_IN_W    2
#define REDMULE_EVAL_STATE_COMPUTATION    3
#define REDMULE_EVAL_STATE_STREAM_OUT     4

/* Returns values of Acquire state */
#define REDMULE_ACQUIRE_LOCKED   -1
#define REDMULE_ACQUIRE_READY    0

class redmule_job_t
{

public:

  /* Job parameters */
  int M;
  int N;
  int K;
  int latency;
  /* Job buffers */
  uint32_t *buffer_x;
  uint32_t *buffer_w;
  uint32_t *buffer_z;
  /* Streamer parameters */
  int x_addr;
  int w_addr;
  int z_addr;
};


class redmule_v1 : public vp::component
{

public:

  redmule_v1(js::config *config);

  int build();
  void start();
  void reset(bool active);

  static vp::io_req_status_e req(void *__this, vp::io_req *req);
  static void job_handler(void *_this, vp::clock_event *event);
  static void grant(void *_this, vp::io_req *req);
  static void response(void *_this, vp::io_req *req);
  void check_requests();

  void enqueue_job();

private:

  int nb_master_ports;
  int nb_rows;
  int nb_cols;
  int nb_pipes_per_fma;

  int state;
  int eval_state;

  void set_state(int new_state);
  void set_id(int id);

  void clear_redmule();

  void exec_job();

  vp::io_req_status_e stream_reqs(uint32_t addr, uint32_t *data, int size, bool is_write);

  int done;

  unsigned int *regs;

  redmule_job_t *job;

  void redmule_req(vp::io_req *req);
  vp::io_req_status_e trigger_req(vp::io_req *req, int new_state);
  vp::io_req_status_e submit_req(vp::io_req *req, int new_state);
  vp::io_req_status_e acquire_req(vp::io_req *req);

  vp::trace trace;

  vp::io_slave in;
  vp::io_master *out;
  vp::io_req *reqs;

  vp::wire_master<bool> irq_0;
  vp::wire_master<bool> irq_1;

  vp::clock_event *job_event;

  int pending_req;
  bool stalled;

};

static string get_state_name(int state) {
  switch (state) {
    case REDMULE_STATE_IDLE: return "idle";
    case REDMULE_STATE_ACQUIRE: return "acquire";
    case REDMULE_STATE_J_EVAL: return "eval";
  }
  return "unknown";
}

static string get_eval_state_name(int state) {
  switch (state) {
    case REDMULE_EVAL_STATE_IDLE: return "idle";
    case REDMULE_EVAL_STATE_STREAM_IN_X: return "stream-in_x";
    case REDMULE_EVAL_STATE_STREAM_IN_W: return "stream-in_w";
    case REDMULE_EVAL_STATE_COMPUTATION: return "compute";
    case REDMULE_EVAL_STATE_STREAM_OUT: return "stream-out";
  }
  return "unknown";
}
