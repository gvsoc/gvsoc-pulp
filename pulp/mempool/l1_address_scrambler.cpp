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

class L1_AddressScrambler : public vp::Component
{

public:
    L1_AddressScrambler(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);
    static unsigned int clog2(int value);

    vp::Trace trace;
    vp::IoSlave input_itf;
    vp::IoMaster output_itf;

    bool bypass;
    int num_tiles;
    int seq_mem_size_per_tile;
    int byte_offset;
    int num_banks_per_tile;

    unsigned int addr_width;
    unsigned int seq_total_size;
    unsigned int bank_offset_bits;
    unsigned int tile_id_bits;
    unsigned int seq_per_tile_bits;
    unsigned int seq_total_bits;
    unsigned int constant_bits_lsb;
    unsigned int scramble_bits;
};



L1_AddressScrambler::L1_AddressScrambler(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&L1_AddressScrambler::req);
    this->output_itf.set_resp_meth(&L1_AddressScrambler::response);
    this->output_itf.set_grant_meth(&L1_AddressScrambler::grant);

    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("output", &this->output_itf);

    bypass = get_js_config()->get_child_bool("bypass");
    num_tiles = get_js_config()->get_child_int("num_tiles");
    seq_mem_size_per_tile = get_js_config()->get_child_int("seq_mem_size_per_tile");
    byte_offset = get_js_config()->get_child_int("byte_offset");
    num_banks_per_tile = get_js_config()->get_child_int("num_banks_per_tile");

    addr_width = 32;
    seq_total_size = num_tiles * seq_mem_size_per_tile;
    bank_offset_bits = clog2(num_banks_per_tile);
    tile_id_bits = clog2(num_tiles);
    seq_per_tile_bits = clog2(seq_mem_size_per_tile);
    seq_total_bits = seq_per_tile_bits + tile_id_bits;
    constant_bits_lsb = byte_offset + bank_offset_bits;
    scramble_bits = seq_per_tile_bits - constant_bits_lsb;
}

vp::IoReqStatus L1_AddressScrambler::req(vp::Block *__this, vp::IoReq *req)
{
    L1_AddressScrambler *_this = (L1_AddressScrambler *)__this;

    if (_this->bypass == false && _this->num_tiles > 1)
    {
        uint64_t addr_i = req->get_addr();
        if(addr_i < _this->seq_total_size)
        {
            uint64_t addr_o = 0;
            uint64_t scramble = GET_BITS(addr_i, _this->constant_bits_lsb, _this->seq_per_tile_bits - 1);
            uint64_t tile_id = GET_BITS(addr_i, _this->seq_per_tile_bits, _this->seq_total_bits - 1);
            SET_BITS(addr_o, GET_BITS(addr_i, 0, _this->constant_bits_lsb - 1), 0, _this->constant_bits_lsb - 1);
            SET_BITS(addr_o, GET_BITS(addr_i, _this->seq_total_bits, _this->addr_width - 1), _this->seq_total_bits, _this->addr_width - 1);
            SET_BITS(addr_o, tile_id, _this->constant_bits_lsb, _this->constant_bits_lsb + _this->tile_id_bits - 1);
            SET_BITS(addr_o, scramble, _this->constant_bits_lsb + _this->tile_id_bits, _this->seq_total_bits - 1);
            req->set_addr(addr_o);
            _this->trace.msg("L1_AddressScrambler: addr_i=0x%lx, addr_o=0x%lx\n", addr_i, addr_o);
        }
    }

    return _this->output_itf.req_forward(req);
}

void L1_AddressScrambler::grant(vp::Block *__this, vp::IoReq *req)
{

}

void L1_AddressScrambler::response(vp::Block *__this, vp::IoReq *req)
{
}

unsigned int L1_AddressScrambler::clog2(int value)
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
    return new L1_AddressScrambler(config);
}