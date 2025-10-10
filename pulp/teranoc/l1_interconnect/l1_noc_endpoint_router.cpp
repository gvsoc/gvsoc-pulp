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
#include "floonoc.hpp"

#define GET_BITS(x, start, end) \
    (((x) >> (start)) & ((1U << ((end) - (start) + 1)) - 1))

class L1NocEndpointRouter : public vp::Component
{

public:
    L1NocEndpointRouter(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    std::vector<vp::IoSlave> input_itfs;
    std::vector<vp::IoMaster> output_itfs;

    bool req_mode;
    int nb_tiles_per_group;

    unsigned int constant_bits_lsb;
    unsigned int tile_id_bits;

    unsigned int clog2(int value);
};



L1NocEndpointRouter::L1NocEndpointRouter(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->req_mode = get_js_config()->get_child_bool("req_mode");
    this->nb_tiles_per_group = get_js_config()->get_int("nb_tiles_per_group");

    this->input_itfs.resize(this->nb_tiles_per_group);
    for (int i = 0; i < this->nb_tiles_per_group; i++)
    {
        this->input_itfs[i].set_req_meth(&L1NocEndpointRouter::req);
        this->new_slave_port("input_" + std::to_string(i), &this->input_itfs[i]);
    }
    this->output_itfs.resize(this->nb_tiles_per_group);
    for (int i = 0; i < this->nb_tiles_per_group; i++)
    {
        this->output_itfs[i].set_resp_meth(&L1NocEndpointRouter::response);
        this->output_itfs[i].set_grant_meth(&L1NocEndpointRouter::grant);
        this->new_master_port("output_" + std::to_string(i), &this->output_itfs[i]);
    }

    int num_banks_per_tile = get_js_config()->get_int("num_banks_per_tile");
    int byte_offset = get_js_config()->get_int("byte_offset");
    this->constant_bits_lsb = byte_offset + clog2(num_banks_per_tile);
    this->tile_id_bits = clog2(this->nb_tiles_per_group);
}

vp::IoReqStatus L1NocEndpointRouter::req(vp::Block *__this, vp::IoReq *req)
{
    L1NocEndpointRouter *_this = (L1NocEndpointRouter *)__this;
    if (_this->req_mode)
    {
        vp::IoReq *core_req = (vp::IoReq *)*req->arg_get(FlooNoc::REQ_BURST);
        uint64_t tile_id = GET_BITS(core_req->addr, _this->constant_bits_lsb, _this->constant_bits_lsb + _this->tile_id_bits - 1);
        _this->trace.msg("Forwarding request to tile %ld\n", tile_id);
        return _this->output_itfs[tile_id].req_forward(req);
    }
    else
    {
        uint64_t tile_id = (uint64_t)*req->arg_get(FlooNoc::REQ_SRC_TILE);
        _this->trace.msg("Forwarding response to tile %ld\n", tile_id);
        return _this->output_itfs[tile_id].req_forward(req);
    }
}

void L1NocEndpointRouter::grant(vp::Block *__this, vp::IoReq *req)
{
}

void L1NocEndpointRouter::response(vp::Block *__this, vp::IoReq *req)
{
}

unsigned int L1NocEndpointRouter::clog2(int value)
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
    return new L1NocEndpointRouter(config);
}