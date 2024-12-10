
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

// Task 7 - Disable INEFFICIENT_MEMORY_ACCESS
// #define INEFFICIENT_MEMORY_ACCESS

template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
class Streamer{
    private:
        int d0_stride_, d1_stride_, d2_stride_;
        int d0_length_, d1_length_, d2_length_;
        int d0_count_, d1_count_, d2_count_;
        AddrType base_addr_;
        HwpeType* accel_instance_;
        int bandwidth_in_bytes_;
        uint32_t alignment_in_bytes_; // 32-bit aligned banks in general
    public:
    AddrType ComputeAddressOffset() const;
    AddrType ComputeAddress() const;
    AddrType Iterate();
    void UpdateCount();
    void ResetCount();
    void inline SingleBankTransaction(bool write_enable, AddrType address, DataType* &data, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose);
    void VectorTransaction(int width, int64_t& cycles, DataType* data, bool wmem, bool verbose, bool is_write);
    void VectorStore(DataType* data, int width, int64_t& cycles, bool wmem, bool verbose);
    void VectorLoad(int width, int64_t& cycles, DataType* data, bool wmem, bool verbose);
    Streamer(){};
    Streamer(HwpeType* accel, AddrType baseAddr, int d0Stride, int d1Stride, int d2Stride, int d0Length, int d1Length, int d2Length, int bandwidthInBytes, int alignment = 4){
        accel_instance_ = accel;
        alignment_in_bytes_ = alignment;
        base_addr_ = baseAddr;
        d0_stride_ = d0Stride;
        d1_stride_ = d1Stride;
        d2_stride_ = d2Stride;
        d0_length_ = d0Length;
        d1_length_ = d1Length;
        d2_length_ = d2Length;
        d0_count_  = 0;
        d1_count_  = 0;
        d2_count_  = 0;
        bandwidth_in_bytes_ = bandwidthInBytes;
    } 
    void UpdateParams(AddrType baseAddr, int d0Stride, int d1Stride, int d2Stride, int d0Length, int d1Length, int d2Length, int bandwidthInBytes, int alignment = 4){
      base_addr_ = baseAddr;
      d0_stride_ = d0Stride;
      d1_stride_ = d1Stride;
      d2_stride_ = d2Stride;
      d0_length_ = d0Length;
      d1_length_ = d1Length;
      d2_length_ = d2Length;
      ResetCount();
      alignment_in_bytes_ = alignment;
      bandwidth_in_bytes_ = bandwidthInBytes;
    }
};

template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::ResetCount()
{
    d0_count_ = 0;
    d1_count_ = 0;
    d2_count_ = 0;
}
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::UpdateCount()
{
  if(d0_count_ != d0_length_-1) d0_count_++;
  else if(d1_count_ != d1_length_-1) {d1_count_++, d0_count_=0;}
  else if(d2_count_ != d2_length_-1) {d2_count_++; d1_count_=0; d0_count_=0;}
  else 
  {
      throw std::runtime_error("Counter Size is exhausted\n");
  }
}

template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
AddrType Streamer<AddrType, HwpeType, DataType, BandWidth>::ComputeAddressOffset()const {  
    return d2_count_*d2_stride_ + d1_count_*d1_stride_ + d0_count_*d0_stride_;
}

template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
AddrType Streamer<AddrType, HwpeType, DataType, BandWidth>::ComputeAddress()const {  
    return base_addr_ + ComputeAddressOffset();
}

template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
AddrType Streamer<AddrType, HwpeType, DataType, BandWidth>::Iterate()
{
    AddrType address = ComputeAddress();
    if(!((d0_count_== (d0_length_-1)) && (d1_count_==(d1_length_-1)) && (d2_count_==(d2_length_-1))))
        UpdateCount();
    return address;
}

template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void inline Streamer<AddrType, HwpeType, DataType, BandWidth>::SingleBankTransaction(bool write_enable, AddrType address, DataType* &data, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose)
{
  this->accel_instance_->io_req.init();
  this->accel_instance_->io_req.set_addr(address);
  this->accel_instance_->io_req.set_size(size);
  this->accel_instance_->io_req.set_data(data);
  this->accel_instance_->io_req.set_is_write(write_enable);
  int err = 0;
  if(wmem) {
    #if WMEM_L1==1
        err = this->accel_instance_->tcdm_port.req(&this->accel_instance_->io_req);
    #else 
        err = this->accel_instance_->wmem_port.req(&this->accel_instance_->io_req);
    #endif 

  } else {
    err = this->accel_instance_->tcdm_port.req(&this->accel_instance_->io_req);
  }

  if (err == vp::IO_REQ_OK) {
    int64_t latency = this->accel_instance_->io_req.get_latency();
    if (latency > max_latency) {
      max_latency = latency;
    }

    if (verbose) {
        this->accel_instance_->trace.msg("max_latency = %d, Address =%x, size=%x, latency=%d, we=%d, data[0]=%02x, data[1]=%02x, data[2]=%02x, data[3]=%02x\n", max_latency, address, size, latency, write_enable, (*data)&0xFF, (*(data+1))&0xFF, (*(data+2))&0xFF, (*(data+3))&0xFF);
    }
  }
  else {
    this->accel_instance_->trace.fatal("Unsupported asynchronous reply\n");
  }
}

// Only for single load transaction. So the width should be less than the bandwidth
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::VectorTransaction(int width, int64_t& cycles, DataType* load_data, bool wmem, bool verbose, bool is_write) {
    int64_t max_latency = 0;
    AddrType addr = Iterate();
    const AddrType addr_start_offset = addr % alignment_in_bytes_;
    DataType* data_ptr = load_data;
    int32_t remainder_bytes = width;

    // This if statement takes care of the initial unaligned transaction
    if (remainder_bytes > 0 && addr_start_offset > 0) {
        const int transaction_size = std::min((uint32_t)(alignment_in_bytes_ - addr_start_offset), (uint32_t) remainder_bytes);
        SingleBankTransaction(is_write, addr, data_ptr, cycles, transaction_size, max_latency, wmem, verbose);
        data_ptr += transaction_size;
        addr += transaction_size;
        remainder_bytes -= transaction_size;
    }

    // Aligned accesses of "alignment" size, except for the last one which might be less, that's why the "min" check
    while (remainder_bytes > 0) {
        const int transaction_size = std::min(alignment_in_bytes_, (uint32_t) remainder_bytes);
        SingleBankTransaction(is_write, addr, data_ptr, cycles, transaction_size, max_latency, wmem, verbose);
        data_ptr += transaction_size;
        addr += transaction_size;
        remainder_bytes -= transaction_size;
    }

    cycles += max_latency + 1;
    if(verbose){
        this->accel_instance_->trace.msg(" latency : %d, max_latency : %d\n", max_latency, cycles);
    }
}

// Only for single load transaction. So the width should be less than the bandwidth
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::VectorLoad(int width, int64_t& cycles, DataType* load_data, bool wmem, bool verbose) {
    const bool is_write = false;
    VectorTransaction(width, cycles, load_data, wmem, verbose, is_write);
}

template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::VectorStore(DataType* data, int width, int64_t& cycles, bool wmem, bool verbose) // Only for single load transaction. So the width should be less than the bandwidth 
{
    const bool is_write = true;
    VectorTransaction(width, cycles, data, wmem, verbose, is_write);
}
#endif
