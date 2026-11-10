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

// Cheshire SoC registers of the Chimera host, see cheshire_regs.py

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <pulp/chips/chimera_v2/cheshire_regs/cheshire_regs_config.hpp>

#define CHESHIRE_NB_SCRATCH            16
#define CHESHIRE_SCRATCH_EOC           2
#define CHESHIRE_BOOT_MODE_OFFSET      0x40
#define CHESHIRE_RTC_FREQ_OFFSET       0x44
#define CHESHIRE_PLATFORM_ROM_OFFSET   0x48
#define CHESHIRE_NUM_INT_HARTS_OFFSET  0x4c
#define CHESHIRE_HW_FEATURES_OFFSET    0x50
#define CHESHIRE_LLC_SIZE_OFFSET       0x54

class CheshireRegs : public vp::Component
{
public:
    CheshireRegs(vp::ComponentConf &config);

    CheshireRegsConfig cfg;

    void reset(bool active) override;

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void entry_sync(vp::Block *__this, uint64_t value);

    vp::Trace trace;
    vp::IoSlave in{&CheshireRegs::req};
    vp::WireSlave<uint64_t> entry_itf;

    uint32_t scratch[CHESHIRE_NB_SCRATCH];
    // Not reset: the loader gives it before the reset is released
    uint32_t platform_rom = 0;
};

CheshireRegs::CheshireRegs(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_slave_port("input", &this->in);

    this->entry_itf.set_sync_meth(&CheshireRegs::entry_sync);
    this->new_slave_port("entry", &this->entry_itf);
}

void CheshireRegs::reset(bool active)
{
    if (active)
    {
        for (int i = 0; i < CHESHIRE_NB_SCRATCH; i++)
        {
            this->scratch[i] = 0;
        }
    }
}

void CheshireRegs::entry_sync(vp::Block *__this, uint64_t value)
{
    CheshireRegs *_this = (CheshireRegs *)__this;
    _this->trace.msg(vp::Trace::LEVEL_INFO, "Setting platform ROM address (addr: 0x%lx)\n", value);
    _this->platform_rom = value;
}

vp::IoReqStatus CheshireRegs::req(vp::Block *__this, vp::IoReq *req)
{
    CheshireRegs *_this = (CheshireRegs *)__this;

    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();
    uint32_t *value = (uint32_t *)req->get_data();

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Access (offset: 0x%lx, size: 0x%lx, is_write: %d)\n",
        offset, size, is_write);

    if (size != 4 || (offset & 3) != 0)
    {
        _this->trace.warning("Invalid access (offset: 0x%lx, size: 0x%lx)\n", offset, size);
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    if (offset < CHESHIRE_NB_SCRATCH * 4)
    {
        int id = offset / 4;
        if (is_write)
        {
            _this->scratch[id] = *value;
            if (id == CHESHIRE_SCRATCH_EOC && (*value & 1))
            {
                _this->time.get_engine()->quit(*value >> 1);
            }
        }
        else
        {
            *value = _this->scratch[id];
        }
        return vp::IO_REQ_DONE;
    }

    // The other registers are read-only
    if (is_write)
    {
        return vp::IO_REQ_DONE;
    }

    switch (offset)
    {
        case CHESHIRE_BOOT_MODE_OFFSET:     *value = _this->cfg.boot_mode; break;
        case CHESHIRE_RTC_FREQ_OFFSET:      *value = _this->cfg.rtc_freq; break;
        case CHESHIRE_PLATFORM_ROM_OFFSET:  *value = _this->platform_rom; break;
        case CHESHIRE_NUM_INT_HARTS_OFFSET: *value = _this->cfg.num_int_harts; break;
        case CHESHIRE_HW_FEATURES_OFFSET:   *value = _this->cfg.hw_features; break;
        case CHESHIRE_LLC_SIZE_OFFSET:      *value = _this->cfg.llc_size; break;
        default:                            *value = 0; break;
    }

    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new CheshireRegs(config);
}
