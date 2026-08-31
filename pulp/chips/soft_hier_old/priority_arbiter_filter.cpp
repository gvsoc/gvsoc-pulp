/*
 * Copyright (C) 2024 ETH Zurich and University of Bologna
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
 * Authors: Chi Zhang, ETHz <chizhang@iis.ee.ethz.ch>
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <math.h>

class PriorityArbiterFilter : public vp::Component
{

public:
    PriorityArbiterFilter(vp::ComponentConf &config);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:
    vp::Trace trace;

    vp::IoMaster output_port;
    vp::IoSlave input_port;

    int bank_width;
    int64_t last_access_cycle;
    int64_t acc_latency;
};

PriorityArbiterFilter::PriorityArbiterFilter(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
    int bank_width = this->get_js_config()->get_child_int("bank_width");
    this->bank_width = bank_width;
    this->last_access_cycle = 0;
    this->acc_latency = 0;

    this->new_master_port("out", &this->output_port);
    this->new_slave_port("input", &this->input_port);
    this->input_port.set_req_meth(&PriorityArbiterFilter::req);
}

vp::IoReqStatus PriorityArbiterFilter::req(vp::Block *__this, vp::IoReq *req)
{
    PriorityArbiterFilter *_this = (PriorityArbiterFilter *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();
    if (size != _this->bank_width)
    {
        _this->trace.fatal("[PriorityArbiterFilter] Received IO req with size %d, but expect %d\n", size, _this->bank_width);
    }

    //Forward Reqeust
    vp::IoReq * bank_req = new vp::IoReq;
    bank_req->init();
    bank_req->set_addr(offset);
    bank_req->set_size(size);
    bank_req->set_data(data);
    bank_req->set_is_write(is_write);
    vp::IoReqStatus status = _this->output_port.req_forward(bank_req);
    if (status != vp::IoReqStatus::IO_REQ_OK)
    {
        _this->trace.fatal("[PriorityArbiterFilter] Forward IO bank_req %p error\n", bank_req);
    }
    delete bank_req;

    //Recalculate latency
    int64_t current_access_cycle = _this->clock.get_cycles();
    if (current_access_cycle < _this->last_access_cycle)
    {
        _this->trace.fatal("[PriorityArbiterFilter] Timming error, current_access_cycle(%d) < last_access_cycle(%d)\n", current_access_cycle, _this->last_access_cycle);
    }
    int64_t diff = current_access_cycle - _this->last_access_cycle;
    _this->acc_latency = (diff < _this->acc_latency)? (_this->acc_latency - diff) : 0;
    req->inc_latency(_this->acc_latency);
    _this->last_access_cycle = current_access_cycle;
    _this->acc_latency += 1;

    return vp::IoReqStatus::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new PriorityArbiterFilter(config);
}
