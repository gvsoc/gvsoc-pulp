
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
 * Authors: Arpan Suravi Prasad, ETH Zurich (prasadar@iis.ee.ethz.ch)
 */
#ifndef STREAMER_H
#define STREAMER_H
#include "datatype.hpp"
#include "vp/itf/io.hpp"

static const AddrType bank_alignment = 4;

template<typename HwpeType, int BandWidth>
class Streamer{
    private:
        int d0_stride_, d1_stride_, d2_stride_;
        int d0_length_, d1_length_, d2_length_;
        int d0_count_, d1_count_, d2_count_;
        AddrType base_addr_;
        HwpeType* accel_instance_;
        vp::Trace* trace;
        vp::IoReq* io_req;
        vp::IoMaster* tcdm_port;
        vp::IoMaster* wmem_port;

    AddrType ComputeAddressOffset() const;
    AddrType ComputeAddress() const;
    void UpdateCount();
    void ResetCount();
    void inline SingleBankTransaction(AddrType address, uint8_t* &data, int size, int64_t& cycles, int64_t& max_latency, bool wmem, bool is_write, bool verbose);
    void VectorTransaction(uint8_t* data, int size, int64_t& cycles, bool wmem, bool is_write, bool verbose);

    public:
    void VectorStore(uint8_t* data, int size, int64_t& cycles, bool wmem, bool verbose);
    void VectorLoad(uint8_t* data, int size, int64_t& cycles, bool wmem, bool verbose);
    Streamer(){};
    Streamer(HwpeType* accel, vp::Trace* trace_, vp::IoReq* io_req_, vp::IoMaster* tcdm_port_, vp::IoMaster* wmem_port_){
        accel_instance_ = accel;
        trace = trace_;
        io_req = io_req_;
        tcdm_port = tcdm_port_;
        wmem_port = wmem_port_;
    } 
    void Init(AddrType baseAddr, int d0Stride, int d1Stride, int d2Stride, int d0Length, int d1Length, int d2Length){
      base_addr_ = baseAddr;
      d0_stride_ = d0Stride;
      d1_stride_ = d1Stride;
      d2_stride_ = d2Stride;
      d0_length_ = d0Length;
      d1_length_ = d1Length;
      d2_length_ = d2Length;
      ResetCount();
    }
};

template<typename HwpeType, int BandWidth>
void Streamer<HwpeType, BandWidth>::ResetCount()
{
    d0_count_ = 0;
    d1_count_ = 0;
    d2_count_ = 0;
}
template<typename HwpeType, int BandWidth>
void Streamer<HwpeType, BandWidth>::UpdateCount()
{
  if(d0_count_ != d0_length_-1) d0_count_++;
  else if(d1_count_ != d1_length_-1) {d1_count_++, d0_count_=0;}
  else if(d2_count_ != d2_length_-1) {d2_count_++; d1_count_=0; d0_count_=0;}
  else {
    d2_count_ = 0; d1_count_ = 0; d0_count_ = 0;
    trace->msg(vp::Trace::LEVEL_WARNING, "Restarting counters due to overflow\n");
  }
}

template<typename HwpeType, int BandWidth>
AddrType Streamer<HwpeType, BandWidth>::ComputeAddressOffset()const {  
    return d2_count_*d2_stride_ + d1_count_*d1_stride_ + d0_count_*d0_stride_;
}

template<typename HwpeType, int BandWidth>
AddrType Streamer<HwpeType, BandWidth>::ComputeAddress()const {  
    return base_addr_ + ComputeAddressOffset();
}

template<typename HwpeType, int BandWidth>
void inline Streamer<HwpeType, BandWidth>::SingleBankTransaction(AddrType address, uint8_t* &data, int size, int64_t& cycles, int64_t& max_latency, bool wmem, bool write_enable, bool verbose)
{
  io_req->init();
  io_req->set_addr(address);
  io_req->set_size(size);
  io_req->set_data(data);
  io_req->set_is_write(write_enable);
  int err = 0;
  if(wmem) {
    #if WMEM_L1==1
        err = tcdm_port->req(io_req);
    #else 
        err = wmem_port->req(io_req);
    #endif 

  } else {
    err = tcdm_port->req(io_req);
  }

  if (err == vp::IO_REQ_OK) {
    int64_t latency = io_req->get_latency();
    if (latency > max_latency) {
      max_latency = latency;
    }

    if (verbose) {
        trace->msg("max_latency = %d, Address =%x, size=%x, latency=%d, we=%d, data[0]=%02x, data[1]=%02x, data[2]=%02x, data[3]=%02x\n", max_latency, address, size, latency, write_enable, (*data)&0xFF, (*(data+1))&0xFF, (*(data+2))&0xFF, (*(data+3))&0xFF);
    }
  }
  else {
    trace->fatal("Unsupported asynchronous reply\n");
  }
}

// Only for single load transaction. So the width should be less than the bandwidth
template<typename HwpeType, int BandWidth>
void Streamer<HwpeType, BandWidth>::VectorTransaction(uint8_t* data, int size, int64_t& cycles, bool wmem, bool is_write, bool verbose) {
    int64_t max_latency = 0;
    AddrType addr = ComputeAddress();
    const AddrType addr_start_offset = addr % bank_alignment;
    uint8_t* data_ptr = data;
    int32_t remainding_size = size;

    // This if statement takes care of the initial unaligned transaction
    if (remainding_size > 0 && addr_start_offset > 0) {
        const int transaction_size = std::min((uint32_t)(bank_alignment - addr_start_offset), (uint32_t) remainding_size);
        SingleBankTransaction(addr, data_ptr, transaction_size, cycles, max_latency, wmem, is_write, verbose);
        data_ptr += transaction_size;
        addr += transaction_size;
        remainding_size -= transaction_size;
    }

    // Aligned accesses of "bank_alignment" size, except for the last one which might be less, that's why the "min" check
    while (remainding_size > 0) {
        const int transaction_size = std::min(bank_alignment, (uint32_t) remainding_size);
        SingleBankTransaction(addr, data_ptr, transaction_size, cycles, max_latency, wmem, is_write, verbose);
        data_ptr += transaction_size;
        addr += transaction_size;
        remainding_size -= transaction_size;
    }

    UpdateCount();

    cycles += max_latency + 1;
    if(verbose){
        trace->msg(" latency : %d, max_latency : %d\n", max_latency, cycles);
    }
}

// Only for single load transaction. So the width should be less than the bandwidth
template<typename HwpeType, int BandWidth>
void Streamer<HwpeType, BandWidth>::VectorLoad(uint8_t* data, int size, int64_t& cycles, bool wmem, bool verbose) {
    const bool is_write = false;
    VectorTransaction(data, size, cycles, wmem, is_write, verbose);
}

template<typename HwpeType, int BandWidth>
void Streamer<HwpeType, BandWidth>::VectorStore(uint8_t* data, int size, int64_t& cycles, bool wmem, bool verbose) // Only for single load transaction. So the width should be less than the bandwidth 
{
    const bool is_write = true;
    VectorTransaction(data, size, cycles, wmem, is_write, verbose);
}
#endif
