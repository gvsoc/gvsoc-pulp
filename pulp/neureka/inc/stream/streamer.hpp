
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
#ifndef GVSoC 
#define GVSoC 
#endif

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
        int alignment_in_bytes_; // 32-bit aligned banks in general
    public:
    AddrType ComputeAddressOffset() const;
    AddrType ComputeAddress() const;
    AddrType Iterate();
    void UpdateCount();
    void ResetCount();
    void inline SingleBankTransaction(bool write_enable, AddrType address, DataType* &data, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose);
    void MisalignedPreambleLoad( bool write_enable, AddrType address, DataType* &data, int& preamble_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose);
    void AlignedLoad( bool write_enable, AddrType address, DataType* &data, int& aligned_width, int offset_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose);
    void MisalignedPostambleLoad( bool write_enable, AddrType address, DataType* &data, int& postamble_width, int offset_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose);
    void MisalignedPreambleStore( bool write_enable, AddrType address, DataType* &data, int& preamble_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose);
    void AlignedStore( bool write_enable, AddrType address, DataType* &data, int& aligned_width, int offset_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose);
    void MisalignedPostambleStore( bool write_enable, AddrType address, DataType* &data, int& postamble_width, int offset_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose);
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

#ifdef GVSoC
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
    int32_t data_word = ((*data) & 0xFF) + (((*(data+1)) & 0xFF)<<8) + (((*(data+2)) & 0xFF)<<16) + (((*(data+3)) & 0xFF)<<24);
    if(verbose)
    {
        // this->accel_instance_->trace.msg("max_latency = %d, Address =%x, size=%x, latency=%d, we=%d, data=0x%x\n", max_latency, address, size, latency, write_enable, data_word);
        this->accel_instance_->trace.msg("max_latency = %d, Address =%x, size=%x, latency=%d, we=%d, data[0]=%d, data[1]=%d, data[2]=%d, data[3]=%d\n", max_latency, address, size, latency, write_enable, (*data)&0xFF, (*(data+1))&0xFF, (*(data+2))&0xFF, (*(data+3))&0xFF);
    }
  }
  else {
    this->accel_instance_->trace.fatal("Unsupported asynchronous reply\n");
  }
  
}
#else
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::SingleBankTransaction(bool write_enable, AddrType address, DataType* &data, int64_t& cycles, int size, int64_t& max_latency, bool wmem)
{
    for(int i=0; i<size; i++)
        std::cout<<"index "<<i<<" we="<<(int)write_enable<<std::hex<<" addr="<<(int)(address+i)<<" data="<<(int)data[i]<<"\n";
}
#endif
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::MisalignedPreambleLoad( bool write_enable, AddrType address, DataType* &data, int& preamble_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose) {
    DataType* load_data = new DataType[4];  
    SingleBankTransaction(write_enable, address, load_data, cycles, size, max_latency, wmem, verbose);
    int start_index = size - preamble_width;
    for(int i=0; i<preamble_width; i++){
        data[i] = load_data[size-preamble_width + i]; 
        // std::cout<<"index="<<size-preamble_width + i<<"\n";
    }
    delete[] load_data;
}
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::AlignedLoad( bool write_enable, AddrType address, DataType* &data, int& aligned_width, int offset_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose ) {
    DataType* load_data = new DataType[size];  
    for(int i=0; i<(aligned_width/size); i++) {
        SingleBankTransaction(write_enable, address+i*4, load_data, cycles, size, max_latency, wmem, verbose);
        for(int j=0; j<size; j++){
            int index = i*4 + j;
            data[index + offset_width] = load_data[j]; 
        }
    }
    delete[] load_data;
}
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::MisalignedPostambleLoad( bool write_enable, AddrType address, DataType* &data, int& postamble_width, int offset_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose ) {
    DataType* load_data = new DataType[4]; 
    SingleBankTransaction(write_enable, address, load_data, cycles, size, max_latency, wmem, verbose);
    for(int i=0; i<postamble_width; i++)
        data[offset_width + i] = load_data[i]; 
    delete[] load_data;
}
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::MisalignedPreambleStore( bool write_enable, AddrType address, DataType* &data, int& preamble_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose ) {
    DataType* store_data = new DataType[preamble_width];  
    for(int i=0; i<preamble_width; i++)
        store_data[i] = data[i];
    SingleBankTransaction(write_enable, address, store_data, cycles, size, max_latency, wmem, verbose);
    delete[] store_data;
}
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::AlignedStore( bool write_enable, AddrType address, DataType* &data, int& aligned_width, int offset_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose ) {
    DataType* store_data = new DataType[size];  
    for(int i=0; i<(aligned_width/size); i++){
        for(int j=0; j<size; j++){
            int index = offset_width + 4*i + j;
            store_data[j]=data[index];
        }
        SingleBankTransaction(write_enable, address+i*4, store_data, cycles, size, max_latency, wmem, verbose);
    }
    delete[] store_data;
}
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::MisalignedPostambleStore( bool write_enable, AddrType address, DataType* &data, int& postamble_width, int offset_width, int64_t& cycles, int size, int64_t& max_latency, bool wmem, bool verbose ) {
    DataType* store_data = new DataType[postamble_width]; 
    for(int i=0; i<postamble_width; i++)
        store_data[i] = data[i+offset_width];
    SingleBankTransaction(write_enable, address, store_data, cycles, size, max_latency, wmem, verbose);
    delete[] store_data;
}
#ifdef INEFFICIENT_MEMORY_ACCESS
    template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
    void Streamer<AddrType, HwpeType, DataType, BandWidth>::VectorLoad(int width, int64_t& cycles, DataType* load_data, bool wmem, bool verbose) // Only for single load transaction. So the width should be less than the bandwidth 
    {
        int64_t max_latency = 0;
        AddrType start_address     = Iterate();
        for(int i=0; i<width; i++){
            this->accel_instance_->io_req.init();
            this->accel_instance_->io_req.set_addr(start_address+i);
            this->accel_instance_->io_req.set_size(1);
            this->accel_instance_->io_req.set_data(&load_data[i]);
            this->accel_instance_->io_req.set_is_write(false);
            int err = 0;
            if(wmem) {
                err = this->accel_instance_->wmem_port.req(&this->accel_instance_->io_req);
            } else {
                err = this->accel_instance_->tcdm_port.req(&this->accel_instance_->io_req);
            }
            if (err == vp::IO_REQ_OK) {
                int64_t latency = this->accel_instance_->io_req.get_latency();
                if (latency > max_latency) {
                    max_latency = latency;
                }
                if(verbose)
                    this->accel_instance_->trace.msg("max_latency = %d, Address =%x, size=%x, latency=%d, we=%d, data=0x%x\n", max_latency, start_address, 1, latency, false, load_data[i]);
            }
            else {
                this->accel_instance_->trace.fatal("Unsupported asynchronous reply\n");
            }
        }


        cycles += max_latency+1;
    }
#else 
    template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
    void Streamer<AddrType, HwpeType, DataType, BandWidth>::VectorLoad(int width, int64_t& cycles, DataType* load_data, bool wmem, bool verbose) // Only for single load transaction. So the width should be less than the bandwidth 
    {
        int64_t max_latency = 0;
        AddrType start_address     = Iterate();
        AddrType end_address       = start_address + width;

        AddrType offset_address    = start_address % alignment_in_bytes_;
        AddrType preamble_address  = start_address - offset_address; // for misaligned address;
        AddrType aligned_address   = offset_address ? ((preamble_address + alignment_in_bytes_ > end_address) ? preamble_address : preamble_address + alignment_in_bytes_) : start_address ;
        AddrType postamble_address = alignment_in_bytes_ * (end_address/alignment_in_bytes_);

        int preamble_width  = start_address-preamble_address ? alignment_in_bytes_ - (start_address-preamble_address) : 0;
        preamble_width = preamble_width > width ? width : preamble_width;
        int aligned_width   = (postamble_address - aligned_address) > 0 ? (postamble_address - aligned_address) : 0;
        aligned_width = aligned_width+preamble_width > width ? (width - preamble_width > 0 ? width-preamble_width : 0) : aligned_width ;  
        int postamble_width = (width - (preamble_width + aligned_width)) > 0 ? (width - (preamble_width + aligned_width)): 0 ;

        if(verbose){
        this->accel_instance_->trace.msg(" start_address : 0x%x, end_address : 0x%x,  offset_address : 0x%x, preamble_address : 0x%x, aligned_address : 0x%x, postamble_address : 0x%x\n", start_address, end_address, offset_address, preamble_address, aligned_address, postamble_address);
        this->accel_instance_->trace.msg(" width : %d, preamble_width : %d, aligned_width : %d, postamble_width : %d\n", width, preamble_width, aligned_width, postamble_width);
        }

        if(preamble_width > 0)
            MisalignedPreambleLoad(false, preamble_address, load_data, preamble_width, cycles, alignment_in_bytes_, max_latency, wmem, verbose );    

        if(aligned_width > 0)
            AlignedLoad(false, aligned_address, load_data, aligned_width, preamble_width, cycles, alignment_in_bytes_, max_latency, wmem, verbose );

        if(postamble_width > 0)
            MisalignedPostambleLoad( false, postamble_address, load_data, postamble_width, preamble_width+aligned_width, cycles, alignment_in_bytes_, max_latency, wmem, verbose );

        cycles += max_latency+1;
        if(verbose){
            this->accel_instance_->trace.msg(" latency : %d, max_latency : %d\n", max_latency, cycles);
        }
    }
#endif
template<typename AddrType, typename HwpeType, typename DataType, int BandWidth>
void Streamer<AddrType, HwpeType, DataType, BandWidth>::VectorStore(DataType* data, int width, int64_t& cycles, bool wmem, bool verbose) // Only for single load transaction. So the width should be less than the bandwidth 
{
    int64_t max_latency = 0;
    AddrType start_address     = Iterate();
    AddrType end_address       = start_address + width;

    AddrType offset_address    = start_address % alignment_in_bytes_;
    AddrType preamble_address  = start_address - offset_address; // for misaligned address;
    AddrType aligned_address   = offset_address ? ((preamble_address + alignment_in_bytes_ > end_address) ? preamble_address : preamble_address + alignment_in_bytes_) : start_address ;
    AddrType postamble_address = alignment_in_bytes_ * (end_address/alignment_in_bytes_);

    int preamble_width  = start_address-preamble_address ? alignment_in_bytes_ - (start_address-preamble_address) : 0;
    preamble_width = preamble_width > width ? width : preamble_width;
    int aligned_width   = (postamble_address - aligned_address) > 0 ? (postamble_address - aligned_address) : 0;
    aligned_width = aligned_width+preamble_width > width ? (width - preamble_width > 0 ? width-preamble_width : 0) : aligned_width ;  
    int postamble_width = (width - (preamble_width + aligned_width)) > 0 ? (width - (preamble_width + aligned_width)): 0 ;

    if(preamble_width > 0)
        MisalignedPreambleStore(true, start_address, data, preamble_width, cycles, preamble_width, max_latency, wmem, verbose );
    if(aligned_width > 0)
        AlignedStore(true, aligned_address, data, aligned_width, preamble_width, cycles, alignment_in_bytes_, max_latency, wmem, verbose );
    if(postamble_width > 0)
        MisalignedPostambleStore( true, postamble_address, data, postamble_width, preamble_width+aligned_width, cycles, postamble_width, max_latency, wmem, verbose );
    cycles += max_latency+1;
    
}
#endif