/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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
 * Authors: Germain Haugou, GreenWaves Technologies (germain.haugou@greenwaves-technologies.com)
 */

#ifndef __PULP_UDMA_I2S_UDMA_I2S_V2_HPP__
#define __PULP_UDMA_I2S_UDMA_I2S_V2_HPP__

#include <vp/vp.hpp>
#include "../udma_impl.hpp"
#include "../archi/udma_i2s_v2.h"


/*
 * I2S
 */

#ifdef HAS_I2S

class I2s_periph;

class I2s_cic_filter {
public:
  I2s_cic_filter();

  bool handle_bit(int din, int pdm_decimation, int pdm_shift, uint32_t *dout);
  void reset();

  int     pdm_pending_bits;
  int64_t pdm_y1_old;
  int64_t pdm_y2_old;
  int64_t pdm_y3_old;
  int64_t pdm_y4_old;
  int64_t pdm_y5_old;
  int64_t pdm_z1_old;
  int64_t pdm_z2_old;
  int64_t pdm_z3_old;
  int64_t pdm_z4_old;
  int64_t pdm_z5_old;
  int64_t pdm_zin1_old;
  int64_t pdm_zin2_old;
  int64_t pdm_zin3_old;
  int64_t pdm_zin4_old;
  int64_t pdm_zin5_old;
};

class I2s_rx_channel : public Udma_rx_channel
{
public:
  I2s_rx_channel(udma *top, I2s_periph *periph, int id, int event_id, string name);
  void handle_rx_bit(int sck, int ws, int bit);

private:
  void reset(bool active);
  I2s_periph *periph;

  I2s_cic_filter *filters[2];
  int id;
  uint32_t pending_samples[2];
  int pending_bits[2];
};

class I2s_periph : public Udma_periph
{
  friend class I2s_rx_channel;

public:
  I2s_periph(udma *top, int id, int itf_id);
  vp::IoReqStatus custom_req(vp::IoReq *req, uint64_t offset);
  void reset(bool active);

protected:
  static void rx_sync(vp::Block *, int sck, int ws, int sd, bool full_duplex,  int channel);

private:

  vp::IoReqStatus i2s_clkcfg_setup_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus i2s_slv_setup_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus i2s_mst_setup_req(int reg_offset, int size, bool is_write, uint8_t *data);
  vp::IoReqStatus i2s_pdm_setup_req(int reg_offset, int size, bool is_write, uint8_t *data);

  static void clkgen_event_routine(vp::Block *__this, vp::ClockEvent *event);
  vp::IoReqStatus check_clkgen0();
  vp::IoReqStatus check_clkgen1();
  vp::IoReqStatus reset_clkgen0();
  vp::IoReqStatus reset_clkgen1();
  void handle_clkgen_tick(int clkgen, int itf);

  vp::Trace     trace;
  vp::I2sSlave ch_itf[2];

  vp_udma_i2s_i2s_clkcfg_setup  r_i2s_clkcfg_setup;
  vp_udma_i2s_i2s_slv_setup     r_i2s_slv_setup;
  vp_udma_i2s_i2s_mst_setup     r_i2s_mst_setup;
  vp_udma_i2s_i2s_pdm_setup     r_i2s_pdm_setup;

  vp::ClockEvent *clkgen0_event;
  vp::ClockEvent *clkgen1_event;

  int sck[2];
  int current_channel;
  int current_bit;
};

#endif


#endif
