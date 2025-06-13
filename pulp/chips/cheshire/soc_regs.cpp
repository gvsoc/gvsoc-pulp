// Generated register defines for cheshire

// Copyright information found in source file:
// Copyright 2022 ETH Zurich and University of Bologna.

// Licensing information found in source file:
// Licensed under Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51

#include "soc_regs.hpp"
#include <vector>
#include <vp/itf/io.hpp>
#include <vp/vp.hpp>

class ControlRegs : public vp::Component
{

  public:
    ControlRegs(vp::ComponentConf &config);

  private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    vp::IoSlave in;
    uint64_t dram_end;

    uint32_t scratch_regs[CHESHIRE_SCRATCH_MULTIREG_COUNT] = {0};
    uint32_t boot_mode                                     = 0;
    uint32_t rtc_freq                                      = 1000000; // Default 1 MHz
    uint32_t platform_rom                                  = 0;
    uint32_t num_int_harts                                 = 1;
    uint32_t hw_features                                   = 0;
    uint32_t llc_size                                      = 0;
    uint32_t vga_params                                    = 0;
};

ControlRegs::ControlRegs(vp::ComponentConf &config) : vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
    this->in.set_req_meth(&ControlRegs::req);
    this->new_slave_port("input", &this->in);
    this->dram_end = this->get_js_config()->get_uint("dram_end");
}

vp::IoReqStatus ControlRegs::req(vp::Block *__this, vp::IoReq *req)
{
    // TODO: everything is treated as get/set of internal registers!

    ControlRegs *_this = (ControlRegs *)__this;
    uint64_t offset    = req->get_addr();
    bool is_write      = req->get_is_write();
    uint64_t size      = req->get_size();
    uint8_t *data      = req->get_data();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n", offset,
                     size, is_write);

    if (offset >= CHESHIRE_SCRATCH_0_REG_OFFSET &&
        offset < CHESHIRE_SCRATCH_0_REG_OFFSET + 4 * CHESHIRE_SCRATCH_MULTIREG_COUNT) {
        // Scratch registers
        int idx = (offset - CHESHIRE_SCRATCH_0_REG_OFFSET) / 4;
        if (is_write && size == 4) {
            _this->scratch_regs[idx] = *(uint32_t *)data;
        } else if (!is_write && size == 4) {
            *(uint32_t *)data = _this->scratch_regs[idx];
        }
        // If it's scratch reg 2, quit and pass return value
        // Right shift by 1 needed to get the correct return value
        if (offset == CHESHIRE_SCRATCH_2_REG_OFFSET) {
            _this->time.get_engine()->quit(_this->scratch_regs[2] >> 1);
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_BOOT_MODE_REG_OFFSET) {
        // Boot mode register
        if (is_write && size == 4) {
            _this->boot_mode = *(uint32_t *)data;
        } else if (!is_write && size == 4) {
            *(uint32_t *)data = _this->boot_mode;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_RTC_FREQ_REG_OFFSET) {
        // RTC frequency register
        if (is_write && size == 4) {
            _this->rtc_freq = *(uint32_t *)data;
        } else if (!is_write && size == 4) {
            *(uint32_t *)data = _this->rtc_freq;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_PLATFORM_ROM_REG_OFFSET) {
        // Platform ROM register
        if (is_write && size == 4) {
            _this->platform_rom = *(uint32_t *)data;
        } else if (!is_write && size == 4) {
            *(uint32_t *)data = _this->platform_rom;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_NUM_INT_HARTS_REG_OFFSET) {
        // Number of interrupt harts register
        if (is_write && size == 4) {
            _this->num_int_harts = *(uint32_t *)data;
        } else if (!is_write && size == 4) {
            *(uint32_t *)data = _this->num_int_harts;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_HW_FEATURES_REG_OFFSET) {
        // Hardware features register
        if (is_write && size == 4) {
            _this->hw_features = *(uint32_t *)data;
        } else if (!is_write && size == 4) {
            *(uint32_t *)data = _this->hw_features;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_LLC_SIZE_REG_OFFSET) {
        // LLC size register
        if (is_write && size == 4) {
            _this->llc_size = *(uint32_t *)data;
        } else if (!is_write && size == 4) {
            *(uint32_t *)data = _this->llc_size;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_VGA_PARAMS_REG_OFFSET) {
        // VGA parameters register
        if (is_write && size == 4) {
            _this->vga_params = *(uint32_t *)data;
        } else if (!is_write && size == 4) {
            *(uint32_t *)data = _this->vga_params;
        }
        return vp::IO_REQ_OK;
    }

    return vp::IO_REQ_INVALID;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ControlRegs(config);
}
