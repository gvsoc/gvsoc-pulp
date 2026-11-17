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

// Platform control registers of the CVA6 standalone testbench (test/hw/cva6tb_regs.sv)

#include <string.h>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <pulp/cva6tb/cva6tb_regs/cva6tb_regs_config.hpp>

class Cva6tbRegs : public vp::Component
{
public:
    Cva6tbRegs(vp::ComponentConf &config);

    void reset(bool active) override;

private:
    static constexpr int NB_REGS = 16;
    static constexpr int REG_BOOT_MODE = 0;
    static constexpr int REG_EOC = 1;
    static constexpr int REG_IRQ = 2;
    static constexpr int REG_MARKER = 3;

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    void write_reg(int index, uint32_t old_value);

    Cva6tbRegsConfig cfg;
    vp::Trace trace;
    vp::IoSlave input_itf{&Cva6tbRegs::req};
    vp::WireMaster<bool> irq_m_itf;
    vp::WireMaster<bool> irq_s_itf;
    uint32_t regs[NB_REGS];
};

Cva6tbRegs::Cva6tbRegs(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("irq_m", &this->irq_m_itf);
    this->new_master_port("irq_s", &this->irq_s_itf);
}

void Cva6tbRegs::reset(bool active)
{
    if (active)
    {
        memset(this->regs, 0, sizeof(this->regs));
        this->regs[REG_BOOT_MODE] = this->cfg.boot_mode;
    }
}

void Cva6tbRegs::write_reg(int index, uint32_t old_value)
{
    uint32_t value = this->regs[index];

    switch (index)
    {
        case REG_BOOT_MODE:
            // Read-only
            this->regs[index] = this->cfg.boot_mode;
            break;

        case REG_EOC:
            if (value & 1)
            {
                this->trace.msg(vp::Trace::LEVEL_INFO, "End of computation (status: %d)\n",
                    value >> 1);
                this->time.get_engine()->quit(value >> 1);
            }
            break;

        case REG_IRQ:
            if ((value ^ old_value) & 1)
            {
                this->irq_m_itf.sync(value & 1);
            }
            if ((value ^ old_value) & 2)
            {
                this->irq_s_itf.sync((value >> 1) & 1);
            }
            break;

        case REG_MARKER:
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Workload phase marker: %d\n", value);
            break;
    }
}

vp::IoReqStatus Cva6tbRegs::req(vp::Block *__this, vp::IoReq *req)
{
    Cva6tbRegs *_this = (Cva6tbRegs *)__this;
    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Access (offset: 0x%lx, size: %ld, is_write: %d)\n",
        offset, size, req->get_is_write());

    // Values before the access, to detect what a write changes
    uint32_t old_regs[NB_REGS];
    memcpy(old_regs, _this->regs, sizeof(old_regs));

    // The register index decodes address bits 5:2 only, like the RTL
    for (uint64_t i = 0; i < size; i++)
    {
        uint64_t byte_addr = offset + i;
        int index = (byte_addr >> 2) & (NB_REGS - 1);
        int byte = byte_addr & 3;
        uint8_t *reg = (uint8_t *)&_this->regs[index];

        if (req->get_is_write())
        {
            uint32_t old_value = old_regs[index];
            reg[byte] = data[i];
            // Side effects once the last byte of the register in this access is written
            if (byte == 3 || i == size - 1)
            {
                _this->write_reg(index, old_value);
            }
        }
        else
        {
            data[i] = reg[byte];
        }
    }

    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Cva6tbRegs(config);
}
