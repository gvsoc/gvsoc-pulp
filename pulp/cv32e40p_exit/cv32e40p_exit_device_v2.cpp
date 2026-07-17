/*
 * Copyright (C) 2026 Fondazione Chips-it
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
 * Authors: Marco Paci, Fondazione Chips-it (marco.paci@chips.it)
 */

/*
 * CV32E40P virtual exit device, io_v2 plane (iss_v2 platforms).
 *
 * Same register map and semantics as the io-plane sibling
 * (cv32e40p_exit_device.cpp): mirrors the UVM virtual peripheral at
 * 0x20000000 — status flags (+0x00, magic 123456789 = PASSED, 1 = FAILED),
 * exit_valid (+0x04), signature registers (+0x08..+0x10). Writes persist in
 * a 256B backing store and read back like testbench memory.
 */

#include <cstring>

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>

#define VP_STATUS_FLAGS_OFFSET  0x00
#define VP_EXIT_VALID_OFFSET    0x04
#define VP_SIG_START_OFFSET     0x08
#define VP_SIG_END_OFFSET       0x0C
#define VP_SIG_WRITE_OFFSET     0x10

class Cv32e40pExitDeviceV2 : public vp::Component
{
public:
    Cv32e40pExitDeviceV2(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::Trace   trace;
    vp::IoSlave in{&Cv32e40pExitDeviceV2::req};

    /* Backing store (256B region): writes persist and read back, like the
     * testbench memory under the virtual peripheral. */
    uint8_t mem[0x100] = {};
};

Cv32e40pExitDeviceV2::Cv32e40pExitDeviceV2(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->in);
}

vp::IoReqStatus Cv32e40pExitDeviceV2::req(vp::Block *__this, vp::IoReq *req)
{
    Cv32e40pExitDeviceV2 *_this = (Cv32e40pExitDeviceV2 *)__this;

    uint32_t offset = (uint32_t)req->get_addr();

    /* Atomics are not supported (no A extension on this platform). */
    if (req->get_opcode() != vp::IoReqOpcode::READ &&
        req->get_opcode() != vp::IoReqOpcode::WRITE)
    {
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    if (req->get_opcode() == vp::IoReqOpcode::READ)
    {
        if (offset + req->get_size() <= sizeof(_this->mem))
            memcpy(req->get_data(), &_this->mem[offset], req->get_size());
        else
            memset(req->get_data(), 0, req->get_size());
        return vp::IO_REQ_DONE;
    }

    if (offset + req->get_size() <= sizeof(_this->mem))
        memcpy(&_this->mem[offset], req->get_data(), req->get_size());

    uint32_t wdata  = (req->get_size() == 4) ? *(uint32_t *)req->get_data() : 0;

    switch (offset)
    {
    case VP_STATUS_FLAGS_OFFSET:
        if (wdata == 123456789U)  /* 0x075BCD15 — TEST PASSED (same magic as UVM VP) */
        {
            _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                "vp_status write: wdata=0x%08x — TEST PASSED, stopping simulation\n", wdata);
            fprintf(stdout, "[cv32e40p_exit] tests_passed=1 exit_value=0x00000000\n");
            fflush(stdout);
            _this->time.get_engine()->quit(0);
        }
        else if (wdata == 1U)  /* TEST FAILED */
        {
            _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                "vp_status write: wdata=0x%08x — TEST FAILED, stopping simulation\n", wdata);
            fprintf(stdout, "[cv32e40p_exit] tests_failed=1 exit_value=0x00000001\n");
            fflush(stdout);
            _this->time.get_engine()->quit(1);
        }
        else
        {
            _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                "vp_status write: wdata=0x%08x (unrecognized status flag — ignored)\n", wdata);
        }
        break;

    case VP_EXIT_VALID_OFFSET:
        _this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "exit_valid asserted: exit_value=0x%08x — stopping simulation\n", wdata);
        fprintf(stdout, "[cv32e40p_exit] exit_valid=1 exit_value=0x%08x\n", wdata);
        fflush(stdout);
        _this->time.get_engine()->quit((int32_t)wdata);
        break;

    case VP_SIG_START_OFFSET:
        _this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "signature_start_address=0x%08x (not implemented)\n", wdata);
        break;

    case VP_SIG_END_OFFSET:
        _this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "signature_end_address=0x%08x (not implemented)\n", wdata);
        break;

    case VP_SIG_WRITE_OFFSET:
        _this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "signature write triggered — stopping simulation (exit_value=0)\n");
        fprintf(stdout, "[cv32e40p_exit] signature write → exit_valid=1 exit_value=0x00000000\n");
        fflush(stdout);
        _this->time.get_engine()->quit(0);
        break;

    default:
        _this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "unknown offset 0x%02x wdata=0x%08x — ignored\n", offset, wdata);
        break;
    }

    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Cv32e40pExitDeviceV2(config);
}
