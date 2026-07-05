/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Minimal MMIO peripheral matching hw/ri5ky_gwt/gv_tb/mmio.sv:
 *   +0x0   PUTCHAR  — write low byte of wdata to stdout
 *   +0x4   EXIT     — call gvsoc time_engine quit(wdata)
 *   +0x8   CYCLE_LO — read low 32 bits of free-running cycle counter
 *   +0xC   CYCLE_HI — read high 32 bits of free-running cycle counter
 *
 * The cycle counter is the simulator's clock cycle count and is
 * independent of the PCCR / PCMR CSRs. Calibration tests use this
 * MMIO so they can measure cycles even when the core runs in fast
 * mode (PCMR.active = 0).
 */

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <cstdio>

class Ri5kyMmio : public vp::Component
{
public:
    Ri5kyMmio(vp::ComponentConf &config);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:
    vp::Trace   trace;
    vp::IoSlave in{&Ri5kyMmio::req};
};

Ri5kyMmio::Ri5kyMmio(vp::ComponentConf &config) : vp::Component(config)
{
    traces.new_trace("trace", &trace, vp::DEBUG);
    new_slave_port("input", &in);
}

vp::IoReqStatus Ri5kyMmio::req(vp::Block *__this, vp::IoReq *req)
{
    Ri5kyMmio *_this = (Ri5kyMmio *)__this;
    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    _this->trace.msg("MMIO access (offset: 0x%x, size: 0x%x, is_write: %d)\n",
                     (int)offset, (int)size, is_write);

    if (is_write)
    {
        uint32_t value = 0;
        for (uint64_t i = 0; i < size && i < 4; i++)
        {
            value |= ((uint32_t)data[i]) << (8 * i);
        }

        switch (offset & 0xc)
        {
            case 0x0:  /* putchar */
            {
                char c = (char)(value & 0xff);
                _this->stdout_write(&c, 1);
                break;
            }
            case 0x4:  /* exit */
                _this->time.get_engine()->quit((int)value);
                break;
            default:
                break;
        }
    }
    else
    {
        uint32_t value = 0;
        switch (offset & 0xc)
        {
            case 0x8:  /* cycle_lo */
                value = (uint32_t)(_this->clock.get_cycles() & 0xffffffffu);
                break;
            case 0xc:  /* cycle_hi */
                value = (uint32_t)((_this->clock.get_cycles() >> 32) & 0xffffffffu);
                break;
            default:
                /* PUTCHAR / EXIT read as 0, matching mmio.sv default. */
                break;
        }
        for (uint64_t i = 0; i < size && i < 4; i++)
            data[i] = (value >> (8 * i)) & 0xff;
        for (uint64_t i = 4; i < size; i++) data[i] = 0;
    }

    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Ri5kyMmio(config);
}
