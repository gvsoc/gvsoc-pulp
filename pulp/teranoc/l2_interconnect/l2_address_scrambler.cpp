/*
 * Copyright (C) 2025 ETH Zurich and University of Bologna
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
 * Authors: Yinrong Li, ETH Zurich (yinrli@student.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>

#define SET_BITS(x, val, start, end) \
    ((x) = ((x) & ~(((1U << ((end) - (start) + 1)) - 1) << (start))) | \
           (((val) & ((1U << ((end) - (start) + 1)) - 1)) << (start)))

#define GET_BITS(x, start, end) \
    (((x) >> (start)) & ((1U << ((end) - (start) + 1)) - 1))

class L2_AddressScrambler : public vp::Component
{

public:
    L2_AddressScrambler(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);
    static unsigned int clog2(int value);

    vp::Trace trace;
    vp::IoSlave input_itf;
    vp::IoMaster output_itf;

    bool bypass;
    uint64_t l2_base_addr;
    uint64_t l2_size;
    unsigned int nb_banks;
    unsigned int bank_width;
    unsigned int interleave;

    unsigned int addr_width;
    unsigned int lsb_constant_bits;
    unsigned int msb_constant_bits;
    unsigned int scramble_bits;
    unsigned int remainder_bits;
};



L2_AddressScrambler::L2_AddressScrambler(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&L2_AddressScrambler::req);
    this->output_itf.set_resp_meth(&L2_AddressScrambler::response);
    this->output_itf.set_grant_meth(&L2_AddressScrambler::grant);

    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("output", &this->output_itf);

    this->bypass = get_js_config()->get_child_bool("bypass");
    this->l2_base_addr = get_js_config()->get_uint("l2_base_addr");
    this->l2_size = get_js_config()->get_uint("l2_size");
    this->nb_banks = get_js_config()->get_uint("nb_banks");
    this->bank_width = get_js_config()->get_uint("bank_width");
    this->interleave = get_js_config()->get_uint("interleave");

    this->addr_width = 32;
    this->lsb_constant_bits = clog2(this->bank_width * this->interleave);
    this->msb_constant_bits = this->addr_width - clog2(this->l2_size);
    this->scramble_bits = this->nb_banks == 1 ? 1 : clog2(this->nb_banks);
    this->remainder_bits = this->addr_width - this->scramble_bits - this->lsb_constant_bits - this->msb_constant_bits;
}

vp::IoReqStatus L2_AddressScrambler::req(vp::Block *__this, vp::IoReq *req)
{
    L2_AddressScrambler *_this = (L2_AddressScrambler *)__this;

    if (_this->bypass == false)
    {
        uint64_t addr_i = req->get_addr();
        if(addr_i >= _this->l2_base_addr && addr_i < (_this->l2_base_addr + _this->l2_size))
        {
            uint64_t addr_o = 0;
            uint64_t lsb_constant = GET_BITS(addr_i, 0, _this->lsb_constant_bits - 1);
            uint64_t msb_constant = GET_BITS(addr_i, _this->addr_width - _this->msb_constant_bits, _this->addr_width - 1);
            uint64_t scramble = GET_BITS(addr_i, _this->lsb_constant_bits, _this->lsb_constant_bits + _this->scramble_bits - 1);
            uint64_t remainder = GET_BITS(addr_i, _this->scramble_bits + _this->lsb_constant_bits, _this->addr_width - _this->msb_constant_bits - 1);
            SET_BITS(addr_o, lsb_constant, 0, _this->lsb_constant_bits - 1);
            SET_BITS(addr_o, msb_constant, _this->addr_width - _this->msb_constant_bits, _this->addr_width - 1);
            SET_BITS(addr_o, scramble, _this->lsb_constant_bits + _this->remainder_bits, _this->addr_width - _this->msb_constant_bits - 1);
            SET_BITS(addr_o, remainder, _this->lsb_constant_bits, _this->lsb_constant_bits + _this->remainder_bits - 1);
            req->set_addr(addr_o);
            _this->trace.msg("L2_AddressScrambler: addr_i=0x%lx, addr_o=0x%lx\n", addr_i, addr_o);
        }
    }

    return _this->output_itf.req_forward(req);
}

void L2_AddressScrambler::grant(vp::Block *__this, vp::IoReq *req)
{

}

void L2_AddressScrambler::response(vp::Block *__this, vp::IoReq *req)
{
}

unsigned int L2_AddressScrambler::clog2(int value)
{
    unsigned int result = 0;
    value--;
    while (value > 0) {
        value >>= 1;
        result++;
    }
    return result;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new L2_AddressScrambler(config);
}