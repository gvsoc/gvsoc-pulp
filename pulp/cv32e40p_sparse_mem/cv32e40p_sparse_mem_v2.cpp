/*
 * Copyright (C) 2026 Fondazione Chips-it
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
 * Authors: Marco Paci, Fondazione Chips-it (marco.paci@chips.it)
 */

/*
 * Background sparse memory, io_v2 plane (iss_v2 platforms).
 *
 * Same contract as the io-plane sibling (cv32e40p_sparse_mem.cpp): mapped as
 * the interconnect's catch-all route, it makes never-written bytes read 0 and
 * writes persist, matching the UVM testbench sparse memory model.
 */

#include <cstdint>
#include <unordered_map>

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>

class Cv32e40pSparseMemV2 : public vp::Component
{
public:
    Cv32e40pSparseMemV2(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::Trace   trace;
    vp::IoSlave in{&Cv32e40pSparseMemV2::req};

    /* Byte-granular backing store: only written bytes are kept. */
    std::unordered_map<uint64_t, uint8_t> store;
};

Cv32e40pSparseMemV2::Cv32e40pSparseMemV2(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->in);
}

vp::IoReqStatus Cv32e40pSparseMemV2::req(vp::Block *__this, vp::IoReq *req)
{
    Cv32e40pSparseMemV2 *_this = (Cv32e40pSparseMemV2 *)__this;

    uint64_t addr = req->get_addr();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "background access (addr: 0x%llx, size: 0x%llx, is_write: %d)\n",
        addr, size, req->get_is_write());

    if (req->get_opcode() == vp::IoReqOpcode::WRITE)
    {
        for (uint64_t i = 0; i < size; i++)
            _this->store[addr + i] = data[i];
    }
    else if (req->get_opcode() == vp::IoReqOpcode::READ)
    {
        for (uint64_t i = 0; i < size; i++)
        {
            auto it = _this->store.find(addr + i);
            data[i] = (it != _this->store.end()) ? it->second : 0;
        }
    }
    else
    {
        /* Atomics are not supported (no A extension on this platform). */
        req->set_resp_status(vp::IO_RESP_INVALID);
    }

    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Cv32e40pSparseMemV2(config);
}
