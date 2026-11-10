/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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

// Chimera SoC registers, see chimera_regs.py

#include <vector>
#include <string.h>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <pulp/chips/chimera_v2/chimera_regs/chimera_regs_config.hpp>

class ChimeraRegs : public vp::Component
{
public:
    ChimeraRegs(vp::ComponentConf &config);

    ChimeraRegsConfig cfg;

    void reset(bool active) override;

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    vp::IoSlave in{&ChimeraRegs::req};

    std::vector<uint32_t> regs;
};

ChimeraRegs::ChimeraRegs(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->in);
    this->regs.resize(this->cfg.nb_regs);
}

void ChimeraRegs::reset(bool active)
{
    if (active)
    {
        std::fill(this->regs.begin(), this->regs.end(), 0);
    }
}

vp::IoReqStatus ChimeraRegs::req(vp::Block *__this, vp::IoReq *req)
{
    ChimeraRegs *_this = (ChimeraRegs *)__this;

    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();
    uint8_t *data = req->get_data();

    uint64_t index = offset / 4;
    uint64_t byte = offset % 4;

    if (size == 0 || byte + size > 4 || index >= _this->regs.size())
    {
        _this->trace.warning("Invalid access (offset: 0x%lx, size: 0x%lx)\n", offset, size);
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    uint8_t *reg = (uint8_t *)&_this->regs[index] + byte;
    if (is_write)
    {
        memcpy(reg, data, size);
    }
    else
    {
        memcpy(data, reg, size);
    }

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Access (offset: 0x%lx, size: 0x%lx, is_write: %d, "
        "reg: 0x%x)\n", offset, size, is_write, _this->regs[index]);

    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ChimeraRegs(config);
}
