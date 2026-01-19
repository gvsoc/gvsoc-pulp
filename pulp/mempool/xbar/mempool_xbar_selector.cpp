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

class MempoolXbarSelector : public vp::Component
{

public:
    MempoolXbarSelector(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    vp::IoSlave input_itf;
    vp::IoMaster output_itf;

    bool interleaver_mode;
    uint64_t output_id;
    int interleaving_bits;
    int stage_bits;
};



MempoolXbarSelector::MempoolXbarSelector(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&MempoolXbarSelector::req);
    this->output_itf.set_resp_meth(&MempoolXbarSelector::response);
    this->output_itf.set_grant_meth(&MempoolXbarSelector::grant);

    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("output", &this->output_itf);

    this->interleaver_mode = get_js_config()->get_child_bool("interleaver_mode");
    this->output_id = get_js_config()->get_child_int("output_id");
    this->interleaving_bits = get_js_config()->get_child_int("interleaving_bits");
    this->stage_bits = get_js_config()->get_child_int("stage_bits");
}

vp::IoReqStatus MempoolXbarSelector::req(vp::Block *__this, vp::IoReq *req)
{
    MempoolXbarSelector *_this = (MempoolXbarSelector *)__this;

    if (_this->interleaver_mode)
    {
        uint64_t offset = req->get_addr();
        uint64_t output_id = (offset >> _this->interleaving_bits) & ((1 << _this->stage_bits) - 1);
        req->arg_push((void *)output_id);
        _this->trace.msg(vp::DEBUG, "MempoolXbarSelector interleaver req addr: 0x%lx, output_id: %ld\n", offset, output_id);
    }
    else
    {
        req->arg_push((void *)_this->output_id);
        _this->trace.msg(vp::DEBUG, "MempoolXbarSelector req addr: 0x%lx, output_id: %ld\n", req->get_addr(), _this->output_id);
    }

    return _this->output_itf.req_forward(req);
}

void MempoolXbarSelector::grant(vp::Block *__this, vp::IoReq *req)
{

}

void MempoolXbarSelector::response(vp::Block *__this, vp::IoReq *req)
{
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new MempoolXbarSelector(config);
}