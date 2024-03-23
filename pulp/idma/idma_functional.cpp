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
#include <vp/itf/wire.hpp>
#include <vp/register.hpp>
#include <stdio.h>
#include <string.h>
#include <cpu/iss/include/offload.hpp>


class IDma : public vp::Component
{

public:
    IDma(vp::ComponentConf &config);

    void reset(bool active);

private:
    void trigger_copy(uint32_t config, uint32_t size);
    uint32_t get_status(uint32_t status);
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);

    vp::Trace trace;
    vp::IoSlave input_itf;

    vp::IoMaster ico_itf;

    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf;

    vp::Register<uint64_t> src;
    vp::Register<uint64_t> dst;
    vp::Register<uint64_t> stride;
    vp::Register<uint32_t> reps;
};



IDma::IDma(vp::ComponentConf &config)
    : vp::Component(config),
    src(*this, "SRC", 64),
    dst(*this, "DST", 64),
    stride(*this, "STRIDE", 64),
    reps(*this, "REPS", 32)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&IDma::req);

    this->new_slave_port("input", &this->input_itf);

    this->new_master_port("ico", &this->ico_itf);

    this->offload_itf.set_sync_meth(&IDma::offload_sync);
    this->new_slave_port("offload", &this->offload_itf);
}


void IDma::reset(bool active)
{

}

void IDma::trigger_copy(uint32_t config, uint32_t size)
{
    vp::IoReq req;

    req.init();

    req.set_addr(this->src.get());
    req.set_size(size);
    req.set_data(new uint8_t[size]);
    req.set_is_write(false);

    int err = this->ico_itf.req(&req);
    if (err == vp::IO_REQ_OK)
    {
        req.prepare();
        req.set_addr(this->dst.get());
        req.set_is_write(true);

        int err = this->ico_itf.req(&req);
        if (err == vp::IO_REQ_OK)
        {
        }
        else if (err == vp::IO_REQ_INVALID)
        {
            this->trace.force_warning("Invalid access (addr: 0x%lx, size: 0x%lx)\n", this->dst.get(), size);
        }
        else
        {
            this->trace.fatal("Unsupported pending or denied access (addr: 0x%lx, size: 0x%lx)\n", this->dst.get(), size);
        }
    }
    else if (err == vp::IO_REQ_INVALID)
    {
        this->trace.force_warning("Invalid access (addr: 0x%lx, size: 0x%lx)\n", this->src.get(), size);
    }
    else
    {
        this->trace.fatal("Unsupported pending or denied access (addr: 0x%lx, size: 0x%lx)\n", this->src.get(), size);
    }

    delete[] req.get_data();

}

uint32_t IDma::get_status(uint32_t status)
{
    switch (status)
    {
        case 0: return 0;
        case 1: return 0;
        case 2: return 0;
        case 3: return 0;
    }

    return 0;
}

void IDma::offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    IDma *_this = (IDma *)__this;
    uint32_t func7 = insn->opcode >> 25;

    switch (func7)
    {
        case 0b0000000:
            _this->src.set((((uint64_t)insn->arg_b) << 32) | insn->arg_a);
            break;
        case 0b0000001:
            _this->dst.set((((uint64_t)insn->arg_b) << 32) | insn->arg_a);
            break;
        case 0b0000110:
            _this->stride.set((((uint64_t)insn->arg_b) << 32) | insn->arg_a);
            break;
        case 0b0000111:
            _this->reps.set(insn->arg_a);
            break;
        case 0b0000011:
            _this->trigger_copy(insn->arg_b, insn->arg_a);
            insn->result = 0;
            break;
        case 0b0000101:
            insn->result = _this->get_status(insn->arg_b);
            break;
        case 0b0000010:
            _this->trigger_copy(insn->arg_b, insn->arg_a);
            insn->result = 0;
            break;
        case 0b0000100:
            insn->result = _this->get_status(insn->arg_b);
            break;
    }
}

vp::IoReqStatus IDma::req(vp::Block *__this, vp::IoReq *req)
{
    IDma *_this = (IDma *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();

    _this->trace.msg("IDma access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, req->get_is_write());

    if (!req->get_is_write() && size == 8)
    {
        *(uint64_t *)data = 0;
    }

    return vp::IO_REQ_OK;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new IDma(config);
}
