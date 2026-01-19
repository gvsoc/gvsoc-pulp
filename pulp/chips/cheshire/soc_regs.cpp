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
        uint32_t num_int_harts                                 = 1;

        // Bitmap for hardware features, all features are enabled by default
        // Bit positions defined by CHESHIRE_HW_FEATURES_*_BIT macros
        uint32_t hw_features =
                (1 << CHESHIRE_HW_FEATURES_BOOTROM_BIT)      |
                (1 << CHESHIRE_HW_FEATURES_LLC_BIT)          |
                (1 << CHESHIRE_HW_FEATURES_UART_BIT)         |
                (1 << CHESHIRE_HW_FEATURES_SPI_HOST_BIT)     |
                (1 << CHESHIRE_HW_FEATURES_I2C_BIT)          |
                (1 << CHESHIRE_HW_FEATURES_GPIO_BIT)         |
                (1 << CHESHIRE_HW_FEATURES_DMA_BIT)          |
                (1 << CHESHIRE_HW_FEATURES_SERIAL_LINK_BIT)  |
                (1 << CHESHIRE_HW_FEATURES_VGA_BIT)          |
                (1 << CHESHIRE_HW_FEATURES_USB_BIT)          |
                (1 << CHESHIRE_HW_FEATURES_AXIRT_BIT)        |
                (1 << CHESHIRE_HW_FEATURES_CLIC_BIT)         |
                (1 << CHESHIRE_HW_FEATURES_IRQ_ROUTER_BIT)   |
                (1 << CHESHIRE_HW_FEATURES_BUS_ERR_BIT);

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
        //  Right shift by 1 needed to get the correct return value
        if (offset == CHESHIRE_SCRATCH_2_REG_OFFSET) {
            _this->time.get_engine()->quit(_this->scratch_regs[2] >> 1);
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_BOOT_MODE_REG_OFFSET) {
        // Boot mode register (read-only)
        if (!is_write && size == 4) {
            *(uint32_t *)data = _this->boot_mode;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_RTC_FREQ_REG_OFFSET) {
        // RTC frequency register (read-only)
        if (!is_write && size == 4) {
            *(uint32_t *)data = _this->rtc_freq;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_PLATFORM_ROM_REG_OFFSET) {
        // Platform ROM register - not implemented
        _this->trace.force_warning("Unhandled Platform ROM register access\n");
        return vp::IO_REQ_INVALID;

    } else if (offset == CHESHIRE_NUM_INT_HARTS_REG_OFFSET) {
        // Number of interrupt harts register (read-only)
        if (!is_write && size == 4) {
            *(uint32_t *)data = _this->num_int_harts;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_HW_FEATURES_REG_OFFSET) {
        // Hardware features register (read-only)
        if (!is_write && size == 4) {
            *(uint32_t *)data = _this->hw_features;
        }
        return vp::IO_REQ_OK;

    } else if (offset == CHESHIRE_LLC_SIZE_REG_OFFSET) {
        // LLC size register - not implemented
        _this->trace.force_warning("Unhandled LLC size register access\n");
        return vp::IO_REQ_INVALID;

    } else if (offset == CHESHIRE_VGA_PARAMS_REG_OFFSET) {
        // VGA parameters register - not implemented
        _this->trace.force_warning("Unhandled VGA parameters register access\n");
        return vp::IO_REQ_INVALID;
    }

    return vp::IO_REQ_INVALID;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ControlRegs(config);
}
