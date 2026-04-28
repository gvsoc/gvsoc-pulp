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

/*
 * Unified address scrambler.
 *
 * Swaps two adjacent bit-fields of an incoming request address, bypassing the
 * request when the address falls outside [base_addr, base_addr + size).
 *
 *   |--lsb_const--|--low_field--|--high_field--|--middle--|--msb_const--|
 *   |             |             |              |          |             |
 *   |-->        kept          <-|--swap--><---|->        kept         <-|
 *
 *  After the swap the layout becomes:
 *   |--lsb_const--|--high_field--|--low_field--|--middle--|--msb_const--|
 *
 * Both the L1 tile-scramble (mempool, teranoc-tile) and the L2 bank-scramble
 * (teranoc) collapse to this single primitive — the Python wrapper computes
 * the field widths from the level-specific topology parameters.
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include "pulp/mempool/common/interco_utils.hpp"

class AddressScrambler : public vp::Component
{
public:
    AddressScrambler(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    vp::IoSlave input_itf;
    vp::IoMaster output_itf;

    bool bypass;
    uint64_t base_addr;
    uint64_t size;
    unsigned int lsb_constant_bits;
    unsigned int low_field_bits;
    unsigned int high_field_bits;
    unsigned int msb_constant_bits;
    unsigned int addr_width;
};

AddressScrambler::AddressScrambler(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&AddressScrambler::req);
    this->output_itf.set_resp_meth(&AddressScrambler::response);
    this->output_itf.set_grant_meth(&AddressScrambler::grant);

    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("output", &this->output_itf);

    this->bypass = get_js_config()->get_child_bool("bypass");
    this->base_addr = get_js_config()->get_uint("base_addr");
    this->size = get_js_config()->get_uint("size");
    this->lsb_constant_bits = get_js_config()->get_uint("lsb_constant_bits");
    this->low_field_bits = get_js_config()->get_uint("low_field_bits");
    this->high_field_bits = get_js_config()->get_uint("high_field_bits");
    this->msb_constant_bits = get_js_config()->get_uint("msb_constant_bits");
    this->addr_width = 32;
}

vp::IoReqStatus AddressScrambler::req(vp::Block *__this, vp::IoReq *req)
{
    AddressScrambler *_this = (AddressScrambler *)__this;

    if (!_this->bypass && _this->low_field_bits > 0 && _this->high_field_bits > 0)
    {
        uint64_t addr_i = req->get_addr();
        if (addr_i >= _this->base_addr && addr_i < _this->base_addr + _this->size)
        {
            unsigned int lo_lsb  = _this->lsb_constant_bits;
            unsigned int hi_lsb  = lo_lsb + _this->low_field_bits;
            unsigned int top     = hi_lsb + _this->high_field_bits;
            unsigned int msb_lsb = _this->addr_width - _this->msb_constant_bits;

            uint64_t low_field  = GET_BITS(addr_i, lo_lsb, hi_lsb - 1);
            uint64_t high_field = GET_BITS(addr_i, hi_lsb, top - 1);

            uint64_t addr_o = 0;
            if (lo_lsb > 0)
            {
                SET_BITS(addr_o, GET_BITS(addr_i, 0, lo_lsb - 1), 0, lo_lsb - 1);
            }
            // swap: high field ends up at the low position, low field at the high position
            SET_BITS(addr_o, high_field, lo_lsb, lo_lsb + _this->high_field_bits - 1);
            SET_BITS(addr_o, low_field,  lo_lsb + _this->high_field_bits, top - 1);
            // pass-through middle (bits between the swapped region and the msb constant)
            if (top < msb_lsb)
            {
                SET_BITS(addr_o, GET_BITS(addr_i, top, msb_lsb - 1), top, msb_lsb - 1);
            }
            // pass-through msb constant
            if (_this->msb_constant_bits > 0)
            {
                SET_BITS(addr_o, GET_BITS(addr_i, msb_lsb, _this->addr_width - 1),
                         msb_lsb, _this->addr_width - 1);
            }

            req->set_addr(addr_o);
            _this->trace.msg("AddressScrambler: addr_i=0x%lx, addr_o=0x%lx\n", addr_i, addr_o);
        }
    }

    return _this->output_itf.req_forward(req);
}

void AddressScrambler::grant(vp::Block *__this, vp::IoReq *req)
{
}

void AddressScrambler::response(vp::Block *__this, vp::IoReq *req)
{
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new AddressScrambler(config);
}
