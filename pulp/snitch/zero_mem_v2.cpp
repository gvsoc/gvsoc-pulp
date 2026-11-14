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
 * io_v2 port of the Snitch cluster zero memory: reads return zero, writes
 * are acknowledged and dropped. Always answers inline (IoV2Sync-compatible).
 */

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <pulp/snitch/zero_mem_v2/zero_mem_config.hpp>

class ZeroMem : public vp::Component
{
public:
    ZeroMem(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    ZeroMemConfig cfg;
    vp::Trace trace;
    vp::IoSlave input_itf{&ZeroMem::req};
    size_t size;
};

ZeroMem::ZeroMem(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_slave_port("input", &this->input_itf);

    this->size = this->cfg.size;
}

vp::IoReqStatus ZeroMem::req(vp::Block *__this, vp::IoReq *req)
{
    ZeroMem *_this = (ZeroMem *)__this;
    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    _this->trace.msg("ZeroMem access (offset: 0x%lx, size: 0x%lx, is_write: %d)\n",
        offset, size, is_write);

    if (offset + size > _this->size)
    {
        _this->trace.force_warning(
            "Received out-of-bound request (reqAddr: 0x%lx, reqSize: 0x%lx, memSize: 0x%lx)\n",
            offset, size, _this->size);
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    if (!is_write && data != NULL)
    {
        memset(data, 0, size);
    }

    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ZeroMem(config);
}
