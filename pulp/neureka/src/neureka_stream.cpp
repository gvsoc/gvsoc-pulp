/*
 * Copyright (C) 2020-2022  GreenWaves Technologies, ETH Zurich, University of Bologna
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
 * Authors: Francesco Conti, University of Bologna & GreenWaves Technologies (f.conti@unibo.it)
 *          Arpan Suravi Prasad, ETH Zurich (prasadar@iis.ee.ethz.ch)
 */

#include <iostream>
#include <cstdlib>
#include <assert.h>
#include "xtensor/xarray.hpp"
#include "xtensor/xio.hpp"
#include "xtensor/xview.hpp"
#include "xtensor/xrandom.hpp"
#include "xtensor/xadapt.hpp"
#include "xtensor/xpad.hpp"
#include <neureka.hpp>

// the NEUREKA can only access L1 memory in the range 0xY000_0000 -- 0xY003_FFFC, where Y=1 or 0
// in the model, Y is ignored
#define NEUREKA_STREAM_L1_MASK 0x0003FFFF

NeurekaStreamAccess::NeurekaStreamAccess(
  Neureka *neureka,
  int base_addr,
  int d0_length,
  int d0_stride,
  int d1_length,
  int d1_stride,
  int d2_length,
  int d2_stride,
  bool debug
) : neureka     ( neureka   ),
    base_addr   ( base_addr ),
    d0_length   ( d0_length ),
    d0_stride   ( d0_stride ),
    d1_length   ( d1_length ),
    d1_stride   ( d1_stride ),
    d2_length   ( d2_length ),
    d2_stride   ( d2_stride ),
    debug       ( debug     ),
    current_addr( 0         )
{
  this->reset_iteration();
  if(this->debug) {
    this->print_config();
  }
}

int NeurekaStreamAccess::get_base_addr() {
  return this->base_addr;
}
int NeurekaStreamAccess::get_d0_length() {
  return this->d0_length;
}
int NeurekaStreamAccess::get_d0_stride() {
  return this->d0_stride;
}
int NeurekaStreamAccess::get_d1_length() {
  return this->d1_length;
}
int NeurekaStreamAccess::get_d1_stride() {
  return this->d1_stride;
}
int NeurekaStreamAccess::get_d2_length() {
  return this->d2_length;
}
int NeurekaStreamAccess::get_d2_stride() {
  return this->d2_stride;
}

void NeurekaStreamAccess::print_config() {
  std::cout << "[STREAMER] base_addr="  << std::hex << this->base_addr << std::dec << std::endl;
  std::cout << "[STREAMER] tot_length=" << this->d0_length << std::endl;
  std::cout << "[STREAMER] d0_stride="  << this->d0_stride << std::endl;
  std::cout << "[STREAMER] d0_length="  << this->d1_length << std::endl;
  std::cout << "[STREAMER] d1_stride="  << this->d1_stride << std::endl;
  std::cout << "[STREAMER] d1_length="  << this->d2_length << std::endl;
  std::cout << "[STREAMER] d2_stride="  << this->d2_stride << std::endl;
}

void NeurekaStreamAccess::reset_iteration() {
  this->wa = 0;
  this->la = 0;
  this->ba = 0;
  this->wc = 1;
  this->lc = 1;
  this->bc = 1;
  this->oc = 0;
}

int NeurekaStreamAccess::iterate() {
  if (this->d1_length < 0) {
    this->current_addr = this->base_addr + this->wa;
  }
  else if(this->d2_length < 0) {
    this->current_addr = this->base_addr + this->la + this->wa;
  }
  else {
    this->current_addr = this->base_addr + this->ba + this->la + this->wa;
  }
  this->oc++;
  if(this->debug) {
    std::cout << "[STREAMER] wa=" << this->wa << " la=" << this->la << " ba=" << this->ba << " oc=" << this->oc << std::endl;
    std::cout << "[STREAMER] wc=" << this->wc << " lc=" << this->lc << " bc=" << this->bc << " oc=" << this->oc << std::endl;
  }
  if((this->wc < this->d1_length) || (this->d1_length < 0)) {
    this->wa += this->d0_stride;
    this->wc += 1;
  }
  else if ((this->lc < this->d2_length) || (this->d2_length < 0)) {
    this->wa = 0;
    this->la += this->d1_stride;
    this->wc = 1;
    this->lc += 1;
  }
  else {
    this->wa = 0;
    this->la = 0;
    this->ba += this->d2_stride;
    this->wc = 1;
    this->lc = 1;
    this->bc += 1;
  }
  return this->current_addr;
}

template <class T>
NeurekaVectorLoad<T>::NeurekaVectorLoad(
  Neureka *neureka,
  int base_addr,
  int d0_length,
  int d0_stride,
  int d1_length,
  int d1_stride,
  int d2_length,
  int d2_stride,
  bool debug
) : NeurekaStreamAccess(neureka, base_addr, d0_length, d0_stride, d1_length, d1_stride, d2_length, d2_stride, debug) {
}

template <class T>
NeurekaVectorLoad<T>::NeurekaVectorLoad() : NeurekaStreamAccess((Neureka *) NULL, 0, 0, 0, 0, 0, 0, 0, false) {
}

// Send the wide read for the NEXT ex(): one access covering the realignment
// padded window (width+4 elements from the word-aligned address), like the
// 9-word port of the streamer. The data lands in `buffer` when the access is
// granted.
template <class T>
void NeurekaVectorLoad<T>::issue(int width, bool w_demux) {
  auto addr = this->iterate();
  this->pending_addr = addr;
  auto width_padded = width + 4;
  auto addr_padded = addr & ~0x3;
  int size = width_padded * sizeof(T);
  assert(size <= STREAM_MAX_WIDTH_BYTES);

  // The weight memory sees the address as it is, the L1 only its offset
  uint64_t req_addr = w_demux ? addr_padded : (addr_padded & NEUREKA_STREAM_L1_MASK);
  this->neureka->stream_issue(req_addr, this->buffer, size, false, w_demux);
}

template <class T>
xt::xarray<T> NeurekaVectorLoad<T>::ex(int width, bool w_demux, int64_t& cycles) {
  auto addr = this->pending_addr;

  if (trace_at_least(this->neureka->trace_level, NEUREKA_L2_STREAM_INOUT)) {
    this->neureka->trace.msg(
      vp::Trace::LEVEL_DEBUG,
      "STREAM ACTIVATION_LOAD: addr=0x%08x size=%dB\n",
      addr & NEUREKA_STREAM_L1_MASK,
      width * static_cast<int>(sizeof(T))
    );
  }

  xt::xarray<T> x = xt::zeros<T>({width});
  for(auto i=0; i<width; i++) {
    xt::view(x, i) = *(T *)(this->buffer + (addr & 0x3) + i*sizeof(T));
  }

  if (trace_at_least(this->neureka->trace_level, NEUREKA_L2_STREAM_INOUT)) {
    std::ostringstream stream;
    xt::print_options::set_line_width(1000);
    stream << "STREAM ACTIVATION_LOAD DATA: " << (this->neureka->trace_format ? std::hex : std::dec) << x << std::dec << "\n";
    const std::string msg = stream.str();
    this->neureka->trace.msg(vp::Trace::LEVEL_DEBUG, msg.c_str());
  }

  // The cycle of the access, its latency is added when it completes
  cycles += 1;
  return x;
}

template <class T>
NeurekaVectorStore<T>::NeurekaVectorStore(
  Neureka *neureka,
  int base_addr,
  int d0_length,
  int d0_stride,
  int d1_length,
  int d1_stride,
  int d2_length,
  int d2_stride,
  bool debug
) : NeurekaStreamAccess(neureka, base_addr, d0_length, d0_stride, d1_length, d1_stride, d2_length, d2_stride, debug) {
}

template <class T>
NeurekaVectorStore<T>::NeurekaVectorStore() : NeurekaStreamAccess((Neureka *) NULL, 0, 0, 0, 0, 0, 0, 0, false) {
}

template <class T>
xt::xarray<T> NeurekaVectorStore<T>::ex(xt::xarray<T> data, int width, int64_t& cycles, int32_t enable) {
  auto addr = this->iterate();
  int width_bytes = width*sizeof(T);
  assert(width_bytes <= STREAM_MAX_WIDTH_BYTES);

  if(enable && width_bytes > 0) {
    for(auto i=0; i<width; i++) {
      *(T *)(this->buffer + i*sizeof(T)) = data(i);
    }
    // One wide write of the whole vector, the HCI cuts it into the bank
    // accesses, misaligned bytes included
    this->neureka->stream_issue(addr & NEUREKA_STREAM_L1_MASK, this->buffer, width_bytes, true, false);
  }
  if (trace_at_least(this->neureka->trace_level, NEUREKA_L2_STREAM_INOUT)) {
    this->neureka->trace.msg(
      vp::Trace::LEVEL_DEBUG,
      "STREAM STORE: addr=0x%08x size=%dB en=%d\n",
      addr & NEUREKA_STREAM_L1_MASK,
      width * static_cast<int>(sizeof(T)),
      enable ? 1 : 0
    );
  }

  if (trace_at_least(this->neureka->trace_level, NEUREKA_L2_STREAM_INOUT)) {
    std::ostringstream stream;
    xt::print_options::set_line_width(1000);
    if(enable) {
      stream << "STREAM STORE DATA: " << (this->neureka->trace_format ? std::hex : std::dec) << data << std::dec << "\n";
    } else {
      stream << "STREAM STORE DATA: write disabled\n";
    }
    const std::string msg = stream.str();
    this->neureka->trace.msg(vp::Trace::LEVEL_DEBUG, msg.c_str());
  }

  // The cycle of the access, its latency is added when it completes
  cycles += 1;

  return data;
}

// template instantiations
template class NeurekaVectorLoad<uint8_t>;
template class NeurekaVectorStore<uint8_t>;
