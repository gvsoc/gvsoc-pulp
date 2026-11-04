// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Siracusa PLL: the pll_config registers as the Siracusa SDK programs them,
// and the frequency they give to the clock domain it drives. See
// siracusa_pll.py.

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/clock.hpp>
#include <stdint.h>
#include <string.h>

#include <ips/pulp/pll/siracusa_pll/siracusa_pll_config.hpp>

#define PLL_STATUS_OFFSET  0x0
#define PLL_CFG1_OFFSET    0x4
#define PLL_CFG2_OFFSET    0x8
#define PLL_CFG3_OFFSET    0xc

#define PLL_STATUS_LOCK_BIT  0
#define PLL_CFG1_PLLEN_BIT   0
#define PLL_CFG2_FBDIV(x)    ((x) & 0x3fff)
#define PLL_CFG2_POSTDIV(x)  (((x) >> 14) & 0xf)
#define PLL_CFG2_VCODIV(x)   (((x) >> 18) & 0x1)

class SiracusaPll : public vp::Component
{
public:
    SiracusaPll(vp::ComponentConf &config);

    void reset(bool active) override;

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    void update_frequency();

    SiracusaPllConfig cfg;

    vp::Trace trace;
    vp::IoSlave in{&SiracusaPll::req};
    vp::ClockMaster clock_itf;

    uint32_t cfg1;
    uint32_t cfg2;
    uint32_t cfg3;
};

SiracusaPll::SiracusaPll(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->in);
    this->new_master_port("clock_out", &this->clock_itf);
}

void SiracusaPll::update_frequency()
{
    int64_t frequency = 0;
    if ((this->cfg1 >> PLL_CFG1_PLLEN_BIT) & 1)
    {
        frequency = (int64_t)PLL_CFG2_FBDIV(this->cfg2) * this->cfg.ref_frequency
            / (PLL_CFG2_POSTDIV(this->cfg2) + 1) / (PLL_CFG2_VCODIV(this->cfg2) + 1);
    }

    this->trace.msg(vp::Trace::LEVEL_INFO, "Setting frequency (frequency: %lld Hz)\n",
        (long long)frequency);

    // A disabled PLL stops its clock, which the clock domain can not do, so it
    // keeps its last frequency
    if (frequency > 0 && this->clock_itf.is_bound())
    {
        this->clock_itf.set_frequency(frequency);
    }
}

vp::IoReqStatus SiracusaPll::req(vp::Block *__this, vp::IoReq *req)
{
    SiracusaPll *_this = (SiracusaPll *)__this;
    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();

    if (size != 4 || (offset & 3))
    {
        _this->trace.force_warning("Unsupported access (offset: 0x%llx, size: %lld)\n",
            (unsigned long long)offset, (long long)size);
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    uint32_t *reg = NULL;
    switch (offset)
    {
        case PLL_CFG1_OFFSET: reg = &_this->cfg1; break;
        case PLL_CFG2_OFFSET: reg = &_this->cfg2; break;
        case PLL_CFG3_OFFSET: reg = &_this->cfg3; break;
    }

    if (req->get_is_write())
    {
        if (reg != NULL)
        {
            memcpy(reg, data, 4);
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Register write (offset: 0x%llx, value: 0x%x)\n",
                (unsigned long long)offset, *reg);
            if (offset != PLL_CFG3_OFFSET)
            {
                _this->update_frequency();
            }
        }
    }
    else
    {
        // Always locked
        uint32_t value = reg ? *reg : (offset == PLL_STATUS_OFFSET ? 1 << PLL_STATUS_LOCK_BIT : 0);
        memcpy(data, &value, 4);
    }

    return vp::IO_REQ_DONE;
}

void SiracusaPll::reset(bool active)
{
    if (active)
    {
        this->cfg1 = this->cfg.cfg1_reset;
        this->cfg2 = this->cfg.cfg2_reset;
        this->cfg3 = this->cfg.cfg3_reset;
    }
    else
    {
        this->update_frequency();
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new SiracusaPll(config);
}
