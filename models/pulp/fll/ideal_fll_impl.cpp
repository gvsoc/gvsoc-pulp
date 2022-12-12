/*
 * Copyright (C) 2022 University of Bologna
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
 * Authors: Nazareno Bruschi, UniBO (<nazareno.bruschi@unibo.it>)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/clock.hpp>
#include <stdio.h>
#include <string.h>


class fll : public vp::component
{

public:

  fll(js::config *config);

  int build();
  void start();
  void reset(bool active);

  static vp::io_req_status_e req(void *__this, vp::io_req *req);

private:

  vp::io_req_status_e set_freq_req(vp::io_req *req);

  vp::clock_master fll_clock_itf;
  vp::io_slave in;
};



fll::fll(js::config *config)
: vp::component(config)
{
}



vp::io_req_status_e fll::set_freq_req(vp::io_req *req)
{
 vp::io_req_status_e err = vp::IO_REQ_OK;

  if (req) {
    uint8_t *data = req->get_data();

    int64_t frequency = ((int64_t)(*(uint32_t *)data));

    this->get_trace()->msg("Setting new frequency (frequency: %ld Hz)\n", frequency);
    this->fll_clock_itf.set_frequency(frequency);
  }
  else {
    err = vp::IO_REQ_INVALID;
  }

  return err;
}



vp::io_req_status_e fll::req(void *__this, vp::io_req *req)
{
  fll *_this = (fll *)__this;

  uint64_t offset = req->get_addr();
  uint64_t size = req->get_size();
  bool is_write = req->get_is_write();

  _this->get_trace()->msg("FLL access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

  int reg_id = offset / 4;

  vp::io_req_status_e err = vp::IO_REQ_OK;

  switch (reg_id) {
    case 0: {
      err = _this->set_freq_req(req);
      break;
    }
  }

  return err;
}



int fll::build()
{
  in.set_req_meth(&fll::req);
  new_slave_port("input", &in);

  new_master_port("clock_out", &fll_clock_itf);
  
  return 0;
}



void fll::start()
{
}



void fll::reset(bool active)
{
}



extern "C" vp::component *vp_constructor(js::config *config)
{
  return new fll(config);
}
