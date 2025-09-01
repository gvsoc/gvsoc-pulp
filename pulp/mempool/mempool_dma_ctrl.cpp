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
 * Authors: Yinrong Li, ETH Zurich (yinrli@student.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <cpu/iss/include/offload.hpp>


class MemPoolDmaCtrl : public vp::Component
{

public:
    MemPoolDmaCtrl(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    vp::IoSlave input_itf;
    vp::WireMaster<IssOffloadInsn<uint32_t>*> dma_offload_itf;

    uint32_t dma_size;
};



MemPoolDmaCtrl::MemPoolDmaCtrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&MemPoolDmaCtrl::req);

    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("dma_offload", &this->dma_offload_itf);

    this->dma_size = 0;
}


vp::IoReqStatus MemPoolDmaCtrl::req(vp::Block *__this, vp::IoReq *req)
{
    MemPoolDmaCtrl *_this = (MemPoolDmaCtrl *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();
    int initiator = req->get_initiator();

    if (size != 4)
    {
        _this->trace.msg("Invalid DMA register access (offset: 0x%x, size: 0x%x, is_write: %d, initiator:%d)\n", offset, size, is_write, initiator);
        return vp::IO_REQ_INVALID;
    }

    if (is_write)
    {
        uint32_t value = *(uint32_t *)data;
        _this->trace.msg("MemPool DMA registers write (offset: 0x%x, size: 0x%x, data:%x, initiator:%d)\n", offset, size, value, initiator);

        if (offset == 0x0)
        {
            _this->trace.msg("Set DMA src address: 0x%x\n", value);
            IssOffloadInsn<uint32_t> insn;
            insn.opcode = 0b0000000 << 25;
            insn.arg_a = value;
            insn.arg_b = 0;
            _this->dma_offload_itf.sync(&insn);
        }
        else if (offset == 0x4)
        {
            _this->trace.msg("Set DMA dst address: 0x%x\n", value);
            IssOffloadInsn<uint32_t> insn;
            insn.opcode = 0b0000001 << 25;
            insn.arg_a = value;
            insn.arg_b = 0;
            _this->dma_offload_itf.sync(&insn);
        }
        else if (offset == 0x8)
        {
            _this->trace.msg("Set DMA size: 0x%x\n", value);
            _this->dma_size = value;
        }
    }
    else
    {
        if (offset == 0x14)
        {
            _this->trace.msg("Start DMA transfer\n");
            IssOffloadInsn<uint32_t> insn;
            insn.opcode = 0b0000010 << 25;
            insn.arg_a = _this->dma_size;
            insn.arg_b = 0;
            _this->dma_offload_itf.sync(&insn);
            *(uint32_t *)data = insn.granted;
        }
        else if (offset == 0x18)
        {
            _this->trace.msg("Get DMA status\n");
            IssOffloadInsn<uint32_t> insn;
            insn.opcode = 0b0000100 << 25;
            insn.arg_b = 2;
            _this->dma_offload_itf.sync(&insn);
            *(uint32_t *)data = insn.result == 0;
        }
        _this->trace.msg("MemPool DMA registers read (offset: 0x%x, size: 0x%x, data:%x, initiator:%d)\n", offset, size, *(uint32_t *)data, initiator);
    }

    return vp::IO_REQ_OK;
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new MemPoolDmaCtrl(config);
}