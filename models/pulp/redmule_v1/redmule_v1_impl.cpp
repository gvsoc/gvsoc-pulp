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

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <stdio.h>
#include <string.h>
#include "archi/archi_redmule_v1.h"
#include "redmule_v1_impl.hpp"
#include <cmath>


redmule_v1::redmule_v1(js::config *config)
: vp::component(config)
{

}


void redmule_v1::reset(bool active)
{
  if (active)
  {
    memset(this->regs, 0, sizeof(unsigned int)*REDMULE_NB_REGS);

    this->clear_redmule();
  }
}

/* Reset states and counters */
void redmule_v1::clear_redmule()
{
  this->state = REDMULE_STATE_IDLE;
  this->eval_state = REDMULE_EVAL_STATE_IDLE;

  this->regs[REDMULE_RUNNING_TASK/4] = -1;

  this->pending_req = 0;
  this->stalled = false;

  this->done = false;
}

/* Stream-in - Compute - Stream-out */
void redmule_v1::job_handler(void *__this, vp::clock_event *event)
{
  redmule_v1 *_this = (redmule_v1 *)__this;

  redmule_job_t *job = _this->job;

  if(_this->state == REDMULE_STATE_J_EVAL)
    _this->trace.msg("Entered job handler (current_state: %s)\n", get_eval_state_name(_this->eval_state).c_str());
  else
    _this->warning.force_warning("Invalid state (current_state: %s)\n", get_state_name(_this->state).c_str());

  switch (_this->eval_state)
  {
    case REDMULE_EVAL_STATE_IDLE:
    {
      /* Streaming buffers */
      job->buffer_x = new uint32_t[job->M*job->N];
      job->buffer_w = new uint32_t[job->N*job->K];
      job->buffer_z = new uint32_t[job->M*job->K];

      _this->eval_state = REDMULE_EVAL_STATE_STREAM_IN_X;

      if(_this->done)
      {
        delete job->buffer_x;
        delete job->buffer_w;
        delete job->buffer_z;

        _this->irq_0.sync(true);
        _this->set_state(REDMULE_STATE_IDLE);
        _this->done = false;
        break;
      }
    }

    case REDMULE_EVAL_STATE_STREAM_IN_X:
    {
      /* X buffer */
      vp::io_req_status_e err = _this->stream_reqs(job->x_addr, job->buffer_x, sizeof(uint32_t)*job->M*job->N, 0);
      _this->eval_state = REDMULE_EVAL_STATE_STREAM_IN_W;

      if (err != vp::IO_REQ_OK)
      {
        _this->warning.force_warning("Invalid stream request (state: REDMULE_EVAL_STATE_STREAM_IN_X)\n");
      }
    }

    case REDMULE_EVAL_STATE_STREAM_IN_W:
    {
      /* W buffer */
      vp::io_req_status_e err = _this->stream_reqs(job->w_addr, job->buffer_w, sizeof(uint32_t)*job->N*job->K, 0);
      _this->eval_state = REDMULE_EVAL_STATE_COMPUTATION;

      if (err != vp::IO_REQ_OK)
      {
        _this->warning.force_warning("Invalid stream request (state: REDMULE_EVAL_STATE_STREAM_IN_W)\n");
      }
    }

    case REDMULE_EVAL_STATE_COMPUTATION:
    {
      _this->exec_job();
      /* Compute the latency */
      job->latency = _this->nb_rows + _this->nb_rows + \
                     ((_this->nb_pipes_per_fma + 1) * (job->N + _this->nb_pipes_per_fma)) * \
                     (ceil((float)(job->M / _this->nb_rows)) * ceil((float)(job->K / (_this->nb_cols * (_this->nb_pipes_per_fma + 1))))) + \
                     1 + _this->nb_cols * (_this->nb_pipes_per_fma + 1) + _this->nb_rows;

      _this->eval_state = REDMULE_EVAL_STATE_STREAM_OUT;
    }

    case REDMULE_EVAL_STATE_STREAM_OUT:
    {
      /* Z buffer */
      vp::io_req_status_e err = _this->stream_reqs(job->z_addr, job->buffer_z, sizeof(uint32_t)*job->M*job->K, 1);

      if(err == vp::IO_REQ_OK)
      {
        _this->eval_state = REDMULE_EVAL_STATE_IDLE;
        _this->done = true;
      }
      else
      {
        _this->warning.force_warning("Invalid stream request (state: REDMULE_EVAL_STATE_STREAM_OUT)\n");
      }

      /* Always done */
      break;
    }
  }

  if(_this->state != REDMULE_STATE_IDLE)
  {
    _this->check_requests();
  }
}


void redmule_v1::set_state(int new_state)
{
  this->trace.msg("Setting new state (new_state: %s)\n", get_state_name(new_state).c_str());
  this->state = new_state;

  this->regs[REDMULE_CHECK_STATE/4] = this->state;
  /* Ensure that other cores or different jobs can take the REDMULE control */
  if(this->state == REDMULE_STATE_IDLE)
  {
    this->regs[REDMULE_STATUS/4] = 0;
    /* REDMULE is free */
    this->set_id(-1);
    this->clear_redmule();
  }
  else
  {
    this->regs[REDMULE_STATUS/4] = 1;
  }
}


void redmule_v1::set_id(int id)
{
  this->trace.msg("Setting running task id (id: %d)\n", id);

  this->regs[REDMULE_RUNNING_TASK/4] = id;
}


/* Every core can request REDMULE acquiring but only when it is ready will be successfully done */
vp::io_req_status_e redmule_v1::acquire_req(vp::io_req *req)
{
  if (!req->get_is_write())
  {
    switch (this->state)
    {
      case REDMULE_STATE_IDLE:
      {
        /* REDMULE backs to idle for one cycle when it finishes one job but more then 0 jobs are already enqueued */
        if(this->regs[REDMULE_STATUS/4] == 0)
        {
          this->regs[REDMULE_ACQUIRE/4] = REDMULE_ACQUIRE_READY;
          this->trace.msg("REDMULE is ready\n");
          this->set_state(REDMULE_STATE_ACQUIRE);
          this->irq_1.sync(true);
          /* TODO: find the id of running task */
          this->set_id(0);
        }
        break;
      }

      default:
      {
        this->regs[REDMULE_ACQUIRE/4] = REDMULE_ACQUIRE_LOCKED;
        this->trace.msg("REDMULE is locked\n");
        break;
      }
    }
    return vp::IO_REQ_OK;
  }

  return vp::IO_REQ_INVALID;
}

/* Enqueue a job request */
vp::io_req_status_e redmule_v1::trigger_req(vp::io_req *req, int new_state)
{
  if(req->get_is_write())
  {
    /* TODO: find id of running task and of submitter */
    if(0 != this->regs[REDMULE_RUNNING_TASK/4])
    {
      /* TODO: find id of running task and of submitter */
      this->warning.force_warning("Invalid request (running_task: %d, submitter_id: %d)\n", this->regs[REDMULE_RUNNING_TASK/4], 0);
      return vp::IO_REQ_INVALID;
    }
    else
    {
      this->set_state(new_state);
      this->redmule_req(req);
      this->enqueue_job();
    }
    return vp::IO_REQ_OK;
  }

  return vp::IO_REQ_INVALID;
}


/* Build REDMULE requests */
void redmule_v1::redmule_req(vp::io_req *req)
{
  this->job->x_addr = this->regs[REDMULE_J_X_ADDR/4];
  this->job->w_addr = this->regs[REDMULE_J_W_ADDR/4];
  this->job->z_addr = this->regs[REDMULE_J_Z_ADDR/4];
  this->job->M = this->regs[REDMULE_J_M/4];
  this->job->N = this->regs[REDMULE_J_N/4];
  this->job->K = this->regs[REDMULE_J_K/4];

  this->trace.msg("Creating new job req (x_addr: %x, w_addr: %x, z_addr: %x, M: %d, N: %d, K: %d)\n",
    this->job->x_addr,
    this->job->w_addr,
    this->job->z_addr,
    this->job->M,
    this->job->N,
    this->job->K
  );
}


/* Programming phase entry point */
vp::io_req_status_e redmule_v1::req(void *__this, vp::io_req *req)
{
  redmule_v1 *_this = (redmule_v1 *)__this;
  vp::io_req_status_e err = vp::IO_REQ_OK;

  uint64_t offset = req->get_addr();
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();
  bool is_write = req->get_is_write();

  _this->trace.msg("REDMULE access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

  int reg_id = offset / 4;

  if (reg_id >= REDMULE_NB_REGS)
  {
    return vp::IO_REQ_INVALID;
  }

  if (!req->get_is_write())
  {
    *(uint32_t *)(req->get_data()) = _this->regs[reg_id];

    switch(reg_id)
    {
      case REDMULE_FINISHED_JOBS/4:
        _this->regs[reg_id] = 0;
        break;
    }
  }
  else
  {
    _this->regs[reg_id] = *(uint32_t *)(req->get_data());
  }

  switch (reg_id)
  {
    case REDMULE_ACQUIRE/4:
    {
      err = _this->acquire_req(req);
      break;
    }

    // /* Job */
    // case REDMULE_J_X_ADDR/4:

    // case REDMULE_J_W_ADDR/4:

    // case REDMULE_J_Z_ADDR/4:

    // case REDMULE_J_M/4:

    // case REDMULE_J_N/4:

    // case REDMULE_J_K/4:
    // {
    //   /* TODO: find the id of running task */
    //   err = _this->redmule_req(req, REDMULE_STATE_J_REQ);
    //   break;
    // }

    case REDMULE_TRIGGER/4:
    {
      err = _this->trigger_req(req, REDMULE_STATE_J_EVAL);
      break;
    }
  }

  return err;
}


/* Matrix-Matrix Multiplication (GeMM) */
void redmule_v1::exec_job()
{
  redmule_job_t *job = this->job;

  int K = job->K;
  int M = job->M;
  int N = job->N;

  float sum = 0;
  for(int m=0; m<M; m++)
  {
    for(int k=0; k<K; k++)
    {
      for(int n=0; n<N; n++)
      {
        float x = *((float *)&job->buffer_x[m*N + n]);
        float w = *((float *)&job->buffer_w[n*K + k]);

        sum += x * w;
      }

      job->buffer_z[m*K + k] = *((uint32_t *)&sum);
      sum = 0;
    }
  }

  this->trace.msg("Computed %dx%dx%d GeMM\n", job->M, job->N, job->K);
}


void redmule_v1::enqueue_job()
{
  if (!this->job_event->is_enqueued())
  {
    event_enqueue(this->job_event, 1);
  }
}


void redmule_v1::check_requests()
{
  // We can continue to send requests if we are not stalled, we didn't send all
  // requests and there are less than 1 pending requests.
  if (!this->stalled && this->pending_req == 0)
  {
    event_enqueue(this->job_event, this->job->latency);
  }
}


void redmule_v1::grant(void *__this, vp::io_req *req)
{
  redmule_v1 *_this = (redmule_v1 *)__this;

  _this->trace.msg("Got grant (req: %p)\n", req);

  _this->stalled = false;

  _this->check_requests();
}


void redmule_v1::response(void *__this, vp::io_req *req)
{
  redmule_v1 *_this = (redmule_v1 *)__this;

  _this->trace.msg("Got response (req: %p)\n", req);

  _this->pending_req--;

  if (_this->eval_state == REDMULE_EVAL_STATE_STREAM_OUT)
  {
    _this->eval_state == REDMULE_EVAL_STATE_IDLE;
    _this->done = true;
  }

  _this->check_requests();
}


/* Fetch/Store from/to L1 */
vp::io_req_status_e redmule_v1::stream_reqs(uint32_t addr, uint32_t *data, int size, bool is_write)
{
  this->trace.msg("New stream request (offset 0x%x, size 0x%x, is_write %d)\n", addr, size, is_write);

  uint32_t offset = addr & (0x400000 - 1);
  uint32_t interleaving_size = 4;
  // debug
  uint32_t _size = size;

  uint32_t req_size = interleaving_size;
  uint32_t req_offset = offset;
  uint8_t *req_data = (uint8_t *)data;

  vp::io_req_status_e err = vp::IO_REQ_INVALID;

  while (_size > 0)
  {
    //printf("new req [(0x%x, 0x%x)] (offset: 0x%x, size 0x%d, is_write %d): done %f%%\n", addr, size, req_offset, req_size, is_write, ((float)(size - _size)/size)*100);
    if (_size < interleaving_size)
    {
      req_size = _size;
    }

    vp::io_req *req = this->out[0].req_new(req_offset, req_data, req_size, is_write);
    err = this->out[0].req(req);

    if (!is_write)
    {
      *(uint32_t *)req_data = *(uint32_t *)req->get_data();
    }

    this->out[0].req_del(req);

    req_offset += req_size;
    req_data += req_size;
    _size -= req_size;
  }


  // if (err)
  // {
  //   if (err == vp::IO_REQ_DENIED)
  //   {
  //     // If the access is not granted, block redmule until it is granted
  //     this->stalled = true;
  //     // We mark it as pending now as we will receive a call to the grant callback
  //     // telling that the requests is now pending
  //     this->pending_req++;
  //   }
  //   else if (err == vp::IO_REQ_PENDING)
  //   {
  //     // Request was granted but is pending, just account it
  //     this->pending_req++;
  //   }
  // }
  // else
  // {
  //   if (!is_write)
  //   {
  //     *(uint32_t *)data = *(uint32_t *)req->get_data();
  //   }
  // }

  // vp::io_req *req = this->out[0].req_new(offset, (uint8_t *)data, size, is_write);
  // err = this->out[0].req(req);

  // if (!is_write)
  // {
  //   *(uint32_t *)data = *(uint32_t *)req->get_data();
  // }

  // this->out[0].req_del(req);

  return err;
}


int redmule_v1::build()
{
  this->traces.new_trace("trace", &this->trace, vp::DEBUG);

  this->nb_master_ports = get_config_int("nb_masters");
  this->nb_rows = get_config_int("nb_rows");
  this->nb_cols = get_config_int("nb_cols");

  this->nb_pipes_per_fma = get_config_int("nb_pipes_per_fma");

  this->out = new vp::io_master[this->nb_master_ports];
  this->reqs = new vp::io_req[this->nb_master_ports];

  this->in.set_req_meth(&this->redmule_v1::req);
  new_slave_port("input", &this->in);

  for (int i=0; i<this->nb_master_ports; i++)
  {
    this->out[i].set_resp_meth(&this->redmule_v1::response);
    this->out[i].set_grant_meth(&this->redmule_v1::grant);
    new_master_port("out_" + std::to_string(i), &this->out[i]);
  }

  new_master_port("irq_0", &this->irq_0);
  new_master_port("irq_1", &this->irq_1);

  this->job_event = event_new(&this->redmule_v1::job_handler);

  this->regs = new unsigned int[REDMULE_NB_REGS];

  this->job = new redmule_job_t;

  return 0;
}


void redmule_v1::start()
{
}


extern "C" void *vp_constructor(js::config *config)
{
  return new redmule_v1(config);
}
