#include <streamer.hpp>
#include <iostream>

Streamer::Streamer(
  vp::io_master* out  ,
  vp::trace* trace
) {
    this->out = out;
    this->trace = trace;
    this->req = this->create_request();
    this->reset_iteration();
}

uint32_t Streamer::get_base_addr() {
  return this->base_addr;
}

uint32_t Streamer::get_d0_length() {
  return this->d0_length;
}

uint32_t Streamer::get_d0_stride() {
  return this->d0_stride;
}

uint32_t Streamer::get_d1_length() {
  return this->d1_length;
}

uint32_t Streamer::get_d1_stride() {
  return this->d1_stride;
}

uint32_t Streamer::get_d2_length() {
  return this->d2_length;
}

uint32_t Streamer::get_d2_stride() {
  return this->d2_stride;
}

void Streamer::print_config() {
  std::cout << "[STREAMER] base_addr="  << std::hex << this->base_addr << std::dec << std::endl;
  std::cout << "[STREAMER] tot_length=" << this->d0_length << std::endl;
  std::cout << "[STREAMER] d0_stride="  << this->d0_stride << std::endl;
  std::cout << "[STREAMER] d0_length="  << this->d1_length << std::endl;
  std::cout << "[STREAMER] d1_stride="  << this->d1_stride << std::endl;
  std::cout << "[STREAMER] d1_length="  << this->d2_length << std::endl;
  std::cout << "[STREAMER] d2_stride="  << this->d2_stride << std::endl;
}

void Streamer::reset_iteration() {
    this->wa = 0;
    this->la = 0;
    this->ba = 0;
    this->wc = 1;
    this->lc = 1;
    this->bc = 1;
    this->oc = 0;
}

void Streamer::config(
    uint32_t base_addr  ,
    uint32_t d0_length  ,
    uint32_t d0_stride  ,
    uint32_t d1_length  ,
    uint32_t d1_stride  ,
    uint32_t d2_length  ,
    uint32_t d2_stride
) {
    this->base_addr = base_addr;
    this->d0_length = d0_length;
    this->d0_stride = d0_stride;
    this->d1_length = d1_length;
    this->d1_stride = d1_stride;
    this->d2_length = d2_length;
    this->d2_stride = d2_stride;
}

uint32_t Streamer::iterate() {
  if (this->d1_length < 0) {
    this->current_addr = this->base_addr + this->wa;
  } else if(this->d2_length < 0) {
    this->current_addr = this->base_addr + this->la + this->wa;
  } else {
    this->current_addr = this->base_addr + this->ba + this->la + this->wa;
  }

  this->oc++;

  /*if(this->debug) {
    std::cout << "[STREAMER] wa=" << this->wa << " la=" << this->la << " ba=" << this->ba << " oc=" << this->oc << std::endl;
    std::cout << "[STREAMER] wc=" << this->wc << " lc=" << this->lc << " bc=" << this->bc << " oc=" << this->oc << std::endl;
  }*/

  if((this->wc < this->d1_length) || (this->d1_length < 0)) {
    this->wa += this->d0_stride;
    this->wc += 1;
  } else if ((this->lc < this->d2_length) || (this->d2_length < 0)) {
    this->wa = 0;
    this->la += this->d1_stride;
    this->wc = 1;
    this->lc += 1;
  } else {
    this->wa = 0;
    this->la = 0;
    this->ba += this->d2_stride;
    this->wc = 1;
    this->lc = 1;
    this->bc += 1;
  }

  return this->current_addr;
}