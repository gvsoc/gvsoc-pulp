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
 * Authors: Chi     Zhang , ETH Zurich (chizhang@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>


class EoC_Registers : public vp::Component
{

public:
    EoC_Registers(vp::ComponentConf &config);

private:
    static void eoc_event_handler(vp::Block *__this, vp::ClockEvent *event);
    void reset(bool active);

    vp::Trace           trace;
    vp::IoMaster        output_itf;
    vp::IoReq*          output_req;
    vp::ClockEvent *    eoc_event;
    uint32_t            eoc_entry;
    uint32_t            interval;
    uint32_t *          access_buffer;
};



EoC_Registers::EoC_Registers(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_master_port("output", &this->output_itf);
    this->output_req    = this->output_itf.req_new(0, 0, 0, 0);
    this->eoc_event     = this->event_new(&EoC_Registers::eoc_event_handler);
    this->eoc_entry     = get_js_config()->get("eoc_entry")->get_int();
    this->interval      = get_js_config()->get("interval")->get_int();
    this->access_buffer = new uint32_t[1];
}

void EoC_Registers::reset(bool active)
{
   this->trace.msg(vp::Trace::LEVEL_TRACE, "[EoC_Registers]: Reset ");
   this->event_enqueue(this->eoc_event, this->interval);
}


void EoC_Registers::eoc_event_handler(vp::Block *__this, vp::ClockEvent *event) {
    EoC_Registers *_this = (EoC_Registers *)__this;

    _this->output_req->init();
    _this->output_req->set_addr(_this->eoc_entry);
    _this->output_req->set_data((uint8_t*)_this->access_buffer);
    _this->output_req->set_size(4);

    //Send request
    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[EoC_Registers] Checking EoC at 0x%x\n", _this->eoc_entry);
    vp::IoReqStatus err = _this->output_itf.req(_this->output_req);

    //Check error
    if (err != vp::IO_REQ_OK) {
        _this->trace.fatal("[EoC_Registers] There was an error while reading addr 0x%x \n", _this->eoc_entry);
    }

    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[EoC_Registers] Value is %d\n", _this->access_buffer[0]);
    if (_this->access_buffer[0] == 1)
    {
        _this->time.get_engine()->quit(0);
    }

    _this->event_enqueue(_this->eoc_event, _this->interval);
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new EoC_Registers(config);
}