/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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
 * CV32E40P virtual exit device for GVSOC.
 *
 * Implements the cv32e40p virtual peripheral status flags (test_programs.rst):
 *
 *   Offset 0x00 (0x2000_0000): test_passed / test_failed flags (write-only sink)
 *   Offset 0x04 (0x2000_0004): assert exit_valid → terminates GVSOC simulation
 *                               exit_value = wdata
 *   Offset 0x08 (0x2000_0008): signature_start_address (write-only sink)
 *   Offset 0x0C (0x2000_000C): signature_end_address   (write-only sink)
 *   Offset 0x10 (0x2000_0010): signature write trigger  (write-only sink)
 *                               also asserts exit_valid with exit_value = 0
 */

#include <cstring>

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

#define VP_STATUS_FLAGS_OFFSET  0x00
#define VP_EXIT_VALID_OFFSET    0x04
#define VP_SIG_START_OFFSET     0x08
#define VP_SIG_END_OFFSET       0x0C
#define VP_SIG_WRITE_OFFSET     0x10

class Cv32e40pExitDevice : public vp::Component
{
public:
    Cv32e40pExitDevice(vp::ComponentConf &config);

private:
    static vp::IoReqStatus handle_req(vp::Block *__this, vp::IoReq *req);

    vp::IoSlave input_itf;
    vp::Trace   trace;
};

Cv32e40pExitDevice::Cv32e40pExitDevice(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->input_itf.set_req_meth(&Cv32e40pExitDevice::handle_req);
    this->new_slave_port("input", &this->input_itf);
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
}

vp::IoReqStatus Cv32e40pExitDevice::handle_req(vp::Block *__this, vp::IoReq *req)
{
    Cv32e40pExitDevice *_this = (Cv32e40pExitDevice *)__this;

    if (!req->get_is_write())
    {
        /* All registers are write-only — return 0 on read */
        memset(req->get_data(), 0, req->get_size());
        return vp::IO_REQ_OK;
    }

    uint32_t offset = (uint32_t)req->get_addr();
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

    return vp::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Cv32e40pExitDevice(config);
}
