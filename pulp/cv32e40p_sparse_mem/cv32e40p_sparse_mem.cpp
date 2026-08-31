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
 * Background sparse memory for the CV32E40P standalone platform.
 *
 * Mapped as the interconnect's default route (size=0 mapping), it serves
 * every access that no explicit device claims. The UVM testbench answers the
 * whole address space from a sparse memory model - a never-written location
 * reads 0, a write persists - so the reference platform must do the same or
 * stray software accesses diverge from the RTL:
 *
 *   - an out-of-map store followed by a load must return the stored value,
 *     not 0 (the store used to be dropped);
 *   - a fetch from an unmapped address must return 0x00000000, which decodes
 *     as an illegal instruction (mcause=2) exactly like the RTL executing
 *     the testbench's zero response - not an instruction access fault
 *     (mcause=1) raised before execution.
 *
 * Contract with the testbench: this component reads 0 for never-written
 * bytes, which matches the cv32e40p environment only because its memory
 * model defaults to zero-fill. A testbench configured to random-fill would
 * diverge from any zero-defaulting reference by construction.
 */

#include <cstdint>
#include <unordered_map>

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

class Cv32e40pSparseMem : public vp::Component
{
public:
    Cv32e40pSparseMem(vp::ComponentConf &config);

private:
    static vp::IoReqStatus handle_req(vp::Block *__this, vp::IoReq *req);

    vp::IoSlave input_itf;
    vp::Trace   trace;

    /* Byte-granular backing store: only written bytes are kept. */
    std::unordered_map<uint64_t, uint8_t> store;
};

Cv32e40pSparseMem::Cv32e40pSparseMem(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->input_itf.set_req_meth(&Cv32e40pSparseMem::handle_req);
    this->new_slave_port("input", &this->input_itf);
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
}

vp::IoReqStatus Cv32e40pSparseMem::handle_req(vp::Block *__this, vp::IoReq *req)
{
    Cv32e40pSparseMem *_this = (Cv32e40pSparseMem *)__this;

    uint64_t addr = req->get_addr();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "background access (addr: 0x%llx, size: 0x%llx, is_write: %d)\n",
        addr, size, req->get_is_write());

    if (req->get_is_write())
    {
        for (uint64_t i = 0; i < size; i++)
            _this->store[addr + i] = data[i];
    }
    else
    {
        for (uint64_t i = 0; i < size; i++)
        {
            auto it = _this->store.find(addr + i);
            data[i] = (it != _this->store.end()) ? it->second : 0;
        }
    }

    return vp::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Cv32e40pSparseMem(config);
}
