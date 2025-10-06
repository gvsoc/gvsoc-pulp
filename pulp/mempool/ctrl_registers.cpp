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
 * Authors: Germain Haugou, ETH Zurich (germain.haugou@iis.ee.ethz.ch)
            Yichao  Zhang , ETH Zurich (yiczhang@iis.ee.ethz.ch)
            Chi     Zhang , ETH Zurich (chizhang@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <cpu/iss/include/offload.hpp>


class CtrlRegisters : public vp::Component
{

public:
    CtrlRegisters(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void wakeup_event_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::Trace trace;
    vp::IoSlave input_itf;
    vp::WireMaster<bool> barrier_ack_itf;
    vp::WireMaster<IssOffloadInsn<uint32_t>*> rocache_cfg_itf;
    vp::ClockEvent * wakeup_event;
    int wakeup_latency;
};



CtrlRegisters::CtrlRegisters(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&CtrlRegisters::req);

    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("barrier_ack", &this->barrier_ack_itf);
    this->new_master_port("rocache_cfg", &this->rocache_cfg_itf);
    this->wakeup_event = this->event_new(&CtrlRegisters::wakeup_event_handler);
    wakeup_latency = get_js_config()->get_child_int("wakeup_latency");
}

void CtrlRegisters::wakeup_event_handler(vp::Block *__this, vp::ClockEvent *event) {
    CtrlRegisters *_this = (CtrlRegisters *)__this;
    _this->barrier_ack_itf.sync(1);
    _this->trace.msg("Control registers wake up signal work and write %d to barrier ack output\n", 1);
}


vp::IoReqStatus CtrlRegisters::req(vp::Block *__this, vp::IoReq *req)
{
    CtrlRegisters *_this = (CtrlRegisters *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();
    int initiator = req->get_initiator();

    _this->trace.msg("Control registers access (offset: 0x%x, size: 0x%x, is_write: %d, data:%x, initiator:%d)\n", offset, size, is_write, *(uint32_t *)data, initiator);

    if (is_write && size == 4)
    {
        uint32_t value = *(uint32_t *)data;
        if (offset == 0 && (value & 1))
        {
            std::cout << "EOC register return value: 0x" << std::hex << ((value - 1) >> 1) << std::endl;
            _this->time.get_engine()->quit(value >> 1);
        }
        if (offset == 4 && value == 0xFFFFFFFF)
        {
            _this->event_enqueue(_this->wakeup_event, _this->wakeup_latency);
        }
        if (offset == 0x48 || offset == 0x4C || offset == 0x50 || offset == 0x54)
        {
            IssOffloadInsn<uint32_t> insn;
            insn.arg_a = (offset - 0x48) >> 2;
            insn.arg_b = 0;
            insn.arg_c = value;
            _this->rocache_cfg_itf.sync(&insn);
        }
        if (offset == 0x58 || offset == 0x5C || offset == 0x60 || offset == 0x64)
        {
            IssOffloadInsn<uint32_t> insn;
            insn.arg_a = (offset - 0x58) >> 2;
            insn.arg_b = 1;
            insn.arg_c = value;
            _this->rocache_cfg_itf.sync(&insn);
        }
    }

    return vp::IO_REQ_OK;
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new CtrlRegisters(config);
}