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
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>


class ZeroMem : public vp::Component
{

public:
    ZeroMem(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    vp::IoSlave input_itf;

    size_t size;
};



ZeroMem::ZeroMem(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&ZeroMem::req);

    this->new_slave_port("input", &this->input_itf);

    this->size = this->get_js_config()->get("size")->get_int();
}



vp::IoReqStatus ZeroMem::req(vp::Block *__this, vp::IoReq *req)
{
    ZeroMem *_this = (ZeroMem *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    _this->trace.msg("ZeroMem access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

    if (offset + size > _this->size)
    {
        _this->trace.force_warning("Received out-of-bound request (reqAddr: 0x%x, reqSize: 0x%x, memSize: 0x%x)\n", offset, size, _this->size);
        return vp::IO_REQ_INVALID;
    }

    if (!is_write)
    {
        memset(data, 0, size);
    }

    return vp::IO_REQ_OK;
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ZeroMem(config);
}