// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// PULP APB SoC control, io_v2 port of `pulp.chips.pulp.apb_soc_impl`: the core
// status register (writing the EOC bit ends the simulation with the 31 LSBs as
// status, as the RTL testbench does), the FC boot address and the boot select
// pads. The JTAG register of the v1 model is left out, as there is no debug
// TAP on the io_v2 platform.

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <string.h>

#include <pulp/chips/pulp/archi/apb_soc.h>

class ApbSocCtrl : public vp::Component
{
public:
    ApbSocCtrl(vp::ComponentConf &config);

    void reset(bool active) override;

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void bootsel_sync(vp::Block *__this, int value);

    vp::Trace trace;
    vp::IoSlave in{&ApbSocCtrl::req};

    vp::WireMaster<uint64_t> bootaddr_itf;
    vp::WireSlave<int>       bootsel_itf;

    uint32_t core_status;
    uint32_t bootaddr;
    int bootsel;
};

ApbSocCtrl::ApbSocCtrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_slave_port("input", &this->in);

    this->bootsel_itf.set_sync_meth(&ApbSocCtrl::bootsel_sync);
    this->new_slave_port("bootsel", &this->bootsel_itf);

    this->new_master_port("bootaddr", &this->bootaddr_itf);

    this->core_status = 0;
    this->bootaddr = 0;
    this->bootsel = 0;
}

void ApbSocCtrl::reset(bool active)
{
    if (active)
    {
        this->core_status = 0;
        this->bootaddr = 0;
    }
}

void ApbSocCtrl::bootsel_sync(vp::Block *__this, int value)
{
    ApbSocCtrl *_this = (ApbSocCtrl *)__this;
    _this->bootsel = value;
}

vp::IoReqStatus ApbSocCtrl::req(vp::Block *__this, vp::IoReq *req)
{
    ApbSocCtrl *_this = (ApbSocCtrl *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "APB SoC ctrl access (offset: 0x%lx, size: 0x%lx, is_write: %d)\n",
        offset, size, is_write);

    if (size != 4)
    {
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    uint32_t *value = (uint32_t *)data;

    if (offset == APB_SOC_CORESTATUS_OFFSET)
    {
        if (!is_write)
        {
            *value = _this->core_status;
        }
        else
        {
            // The 31 LSBs are the status of the platform, the MSB makes it exit
            _this->core_status = *value;

            if ((_this->core_status >> APB_SOC_STATUS_EOC_BIT) & 1)
            {
                _this->time.get_engine()->quit(_this->core_status & 0x7fffffff);
            }
        }
    }
    else if (offset == APB_SOC_PADS_CONFIG)
    {
        if (!is_write)
        {
            *value = _this->bootsel;
        }
    }
    else if (offset == APB_SOC_BOOTADDR_OFFSET)
    {
        if (is_write)
        {
            _this->trace.msg(vp::Trace::LEVEL_INFO, "Setting boot address (addr: 0x%x)\n", *value);
            _this->bootaddr = *value;
            if (_this->bootaddr_itf.is_bound())
            {
                _this->bootaddr_itf.sync(_this->bootaddr);
            }
        }
        else
        {
            *value = _this->bootaddr;
        }
    }
    else if (!is_write)
    {
        // Registers the model does not have read as zero, as in the v1 model
        *value = 0;
    }

    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ApbSocCtrl(config);
}
