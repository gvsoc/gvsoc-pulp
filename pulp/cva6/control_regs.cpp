/*
 * Copyright (C) 2020 ETH Zurich and University of Bologna
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

#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

class ControlRegs : public vp::Component
{

public:

    ControlRegs(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::Trace     trace;

    vp::IoSlave in;
    uint64_t dram_end;
};

ControlRegs::ControlRegs(vp::ComponentConf &config)
: vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->in.set_req_meth(&ControlRegs::req);
    this->new_slave_port("input", &this->in);

    this->dram_end = this->get_js_config()->get_uint("dram_end");
}

vp::IoReqStatus ControlRegs::req(vp::Block *__this, vp::IoReq *req)
{
    ControlRegs *_this = (ControlRegs *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n", offset, size, is_write);

    if (offset == 0)
    {
        if (is_write && size == 8)
        {
            _this->time.get_engine()->quit(*(uint64_t *)data);
        }
    }
    else if (offset == 0x8)
    {
        _this->trace.force_warning("Unhandled dram start address register access\n");
        return vp::IO_REQ_INVALID;
    }
    else if (offset == 0x10)
    {
        if (!is_write && size == 8)
        {
            *(uint64_t *)data = _this->dram_end;
        }
    }
    else if (offset == 0x18)
    {
        _this->trace.force_warning("Unhandled event trigger register access\n");
        return vp::IO_REQ_INVALID;
    }
    else if (offset == 0x20)
    {
        _this->trace.force_warning("Unhandled hw cnt en register access\n");
        return vp::IO_REQ_INVALID;
    }

    return vp::IO_REQ_OK;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ControlRegs(config);
}
