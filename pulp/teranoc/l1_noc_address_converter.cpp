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

class L1NocAddressConverter : public vp::Component
{

public:
    L1NocAddressConverter(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);
    static unsigned int clog2(int value);

    vp::Trace trace;
    vp::IoSlave input_itf;
    vp::IoMaster output_itf;

    bool bypass;
    bool xbar_to_noc;
    int bank_size;
    int byte_offset;
    int num_groups;
    int num_tiles_per_group;
    int num_banks_per_tile;

    unsigned int addr_width;
    unsigned int constant_bits_lsb;
    unsigned int bank_offset_bits;
    unsigned int group_id_bits;
};



L1NocAddressConverter::L1NocAddressConverter(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&L1NocAddressConverter::req);
    this->output_itf.set_resp_meth(&L1NocAddressConverter::response);
    this->output_itf.set_grant_meth(&L1NocAddressConverter::grant);

    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("output", &this->output_itf);

    bypass = get_js_config()->get_child_bool("bypass");
    xbar_to_noc = get_js_config()->get_child_bool("xbar_to_noc");
    bank_size = get_js_config()->get_child_int("bank_size");
    byte_offset = get_js_config()->get_child_int("byte_offset");
    num_groups = get_js_config()->get_child_int("num_groups");
    num_tiles_per_group = get_js_config()->get_child_int("num_tiles_per_group");
    num_banks_per_tile = get_js_config()->get_child_int("num_banks_per_tile");

    addr_width = 32;
    constant_bits_lsb = byte_offset + clog2(num_banks_per_tile) + clog2(num_tiles_per_group);
    bank_offset_bits = clog2(bank_size) - byte_offset;
    group_id_bits = clog2(num_groups);
}

vp::IoReqStatus L1NocAddressConverter::req(vp::Block *__this, vp::IoReq *req)
{
    L1NocAddressConverter *_this = (L1NocAddressConverter *)__this;

    if (_this->bypass == false)
    {
        uint64_t addr_i = req->get_addr();
        if(_this->xbar_to_noc)
        {
            uint64_t addr_o = 0;
            uint64_t constant_lsb = GET_BITS(addr_i, 0, _this->constant_bits_lsb - 1);
            uint64_t bank_offset = GET_BITS(addr_i, _this->constant_bits_lsb + _this->group_id_bits, _this->constant_bits_lsb + _this->group_id_bits + _this->bank_offset_bits - 1);
            uint64_t group_id = GET_BITS(addr_i, _this->constant_bits_lsb, _this->constant_bits_lsb + _this->group_id_bits - 1);
            SET_BITS(addr_o, constant_lsb, 0, _this->constant_bits_lsb - 1);
            SET_BITS(addr_o, bank_offset, _this->constant_bits_lsb, _this->constant_bits_lsb + _this->bank_offset_bits - 1);
            SET_BITS(addr_o, group_id, _this->constant_bits_lsb + _this->bank_offset_bits, _this->constant_bits_lsb + _this->bank_offset_bits + _this->group_id_bits - 1);
            req->set_addr(addr_o);
            _this->trace.msg("L1NocAddressConverter: Xbar to Noc, addr_i=0x%lx, addr_o=0x%lx\n", addr_i, addr_o);
        }
        else
        {
            uint64_t addr_o = 0;
            uint64_t constant_lsb = GET_BITS(addr_i, 0, _this->constant_bits_lsb - 1);
            uint64_t bank_offset = GET_BITS(addr_i, _this->constant_bits_lsb, _this->constant_bits_lsb + _this->bank_offset_bits - 1);
            uint64_t group_id = GET_BITS(addr_i, _this->constant_bits_lsb + _this->bank_offset_bits, _this->constant_bits_lsb + _this->bank_offset_bits + _this->group_id_bits - 1);
            SET_BITS(addr_o, constant_lsb, 0, _this->constant_bits_lsb - 1);
            SET_BITS(addr_o, group_id, _this->constant_bits_lsb, _this->constant_bits_lsb + _this->group_id_bits - 1);
            SET_BITS(addr_o, bank_offset, _this->constant_bits_lsb + _this->group_id_bits, _this->constant_bits_lsb + _this->group_id_bits + _this->bank_offset_bits - 1);
            req->set_addr(addr_o);
            _this->trace.msg("L1NocAddressConverter: Noc to Xbar, addr_i=0x%lx, addr_o=0x%lx\n", addr_i, addr_o);
        }

    }

    return _this->output_itf.req_forward(req);
}

void L1NocAddressConverter::grant(vp::Block *__this, vp::IoReq *req)
{

}

void L1NocAddressConverter::response(vp::Block *__this, vp::IoReq *req)
{
}

unsigned int L1NocAddressConverter::clog2(int value)
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
    return new L1NocAddressConverter(config);
}