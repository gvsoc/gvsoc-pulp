/*
 * Copyright (C) 2025 Fondazione Chips-IT
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
 * Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <stdint.h>

/*****************************************************
*                   Class Definition                 *
*****************************************************/


class SnitchSpatzRegs : public vp::Component
{

public:
    SnitchSpatzRegs(vp::ComponentConf &config);

protected:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    vp::IoSlave         input_itf;

    vp::reg_32 clock_en_reg;
    vp::reg_32 exchange_reg;
    vp::reg_32 start_irq_reg;
    vp::reg_32 done_reg;

    vp::WireMaster<bool> clock_en;
    vp::WireMaster<bool> start_irq;

    vp::Trace trace;
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new SnitchSpatzRegs(config);
}

SnitchSpatzRegs::SnitchSpatzRegs(vp::ComponentConf &config)
    : vp::Component(config)
{
    //Initialize interface
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->input_itf.set_req_meth(&SnitchSpatzRegs::req);
    this->new_slave_port("input", &this->input_itf);

    this->clock_en_reg.set(0x00000000);
    this->exchange_reg.set(0x00000000);
    this->start_irq_reg.set(0x00000000);
    this->done_reg.set(0x00000000);

    this->new_master_port("clock_en", &this->clock_en, this);
    this->new_master_port("start_irq", &this->start_irq, this);

    this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia Snitch Spatz Registers] Instantiated\n");

}

vp::IoReqStatus SnitchSpatzRegs::req(vp::Block *__this, vp::IoReq *req)
{
    SnitchSpatzRegs *_this = (SnitchSpatzRegs *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if (size!=4) {
         _this->trace.fatal("[Magia Snitch Spatz Registers] Memory mapped interface supports only 32 bits (4 bytes) buses\n");
    }

    if (offset == 0x00) {
        if (is_write == 1) {
            uint32_t cnf_w;
            memcpy((uint8_t*)&cnf_w,data,size);
            _this->clock_en_reg.set(cnf_w);
            if (cnf_w==0x01) {
                _this->clock_en.sync(true);
                _this->trace.msg("[Magia Snitch Spatz Registers] Snitch Spatz enable clock\n");
            }
            else if (cnf_w==0x00) {
                _this->clock_en.sync(false);
                _this->trace.msg("[Magia Snitch Spatz Registers] Snitch Spatz disable clock\n");
            }
            else {
                _this->trace.fatal("[Magia Snitch Spatz Registers] Unsupported value\n");
            }
        }
        else {
            uint32_t cnf_r =  _this->clock_en_reg.get();
            memcpy((void *)data, (void *)&cnf_r, size);
            _this->trace.msg("[Magia Snitch Spatz Registers] Snitch Spatz read clock enable register (0x%08x)\n",cnf_r);
        }
    }
    else if (offset == 0x04) {
        if (is_write == 1) {
            uint32_t cnf_w;
            memcpy((uint8_t*)&cnf_w,data,size);
            _this->exchange_reg.set(cnf_w);
            _this->trace.msg("[Magia Snitch Spatz Registers] Snitch Spatz written exchange register (0x%08x)\n",cnf_w);
        }
        else {
            uint32_t cnf_r =  _this->exchange_reg.get();
            memcpy((void *)data, (void *)&cnf_r, size);
            _this->trace.msg("[Magia Snitch Spatz Registers] Snitch Spatz read exchange register (0x%08x)\n",cnf_r);
        }
    }
    else if (offset == 0x08) {
        if (is_write == 1) {
            uint32_t cnf_w;
            memcpy((uint8_t*)&cnf_w,data,size);
            _this->start_irq_reg.set(cnf_w);
            if (cnf_w==0x01) {
                _this->start_irq.sync(true);
                _this->trace.msg("[Magia Snitch Spatz Registers] Snitch Spatz start irq\n");
            }
            else {
                _this->trace.fatal("[Magia Snitch Spatz Registers] Unsupported value\n");
            }
        }
        else {
            _this->trace.fatal("[Magia Snitch Spatz Registers] Unsupported read to register\n");
        }
    }
    return vp::IO_REQ_OK;
}
