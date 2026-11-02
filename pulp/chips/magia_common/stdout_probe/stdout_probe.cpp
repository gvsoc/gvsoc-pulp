/*
 * Copyright (C) 2026 Fondazione Chips-IT
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
 * Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <stdio.h>
#include <string>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>

/*
 * Print monitor of the MAGIA testbench (magia_vip.sv, print_monitor), on the
 * narrow port of a tile towards the NoC.
 *
 * The probe is a wire: requests, responses and retries go through unchanged
 * and in the same cycle. A write to the stdout address is a character of the
 * tile; the line is printed on a newline. The write goes on to the L2, which
 * answers it as any other write, so a character costs the trip to the L2 as
 * in the RTL.
 */

class StdoutProbe : public vp::Component
{
public:
    StdoutProbe(vp::ComponentConf &config);

private:
    static vp::IoReqStatus in_req(vp::Block *__this, vp::IoReq *req);
    static void in_resp_retry(vp::Block *__this, vp::IoRetryChannel channel);
    static vp::IoRespAck out_resp(vp::Block *__this, vp::IoReq *req);
    static void out_retry(vp::Block *__this, vp::IoRetryChannel channel);

    vp::Trace trace;
    vp::IoSlave in{&StdoutProbe::in_req, &StdoutProbe::in_resp_retry};
    vp::IoMaster out{&StdoutProbe::out_retry, &StdoutProbe::out_resp};

    uint64_t addr;
    // The characters of the current line
    std::string line;
};

StdoutProbe::StdoutProbe(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_slave_port("input", &this->in);
    this->new_master_port("output", &this->out);

    this->addr = this->get_js_config()->get_uint("addr");
}

vp::IoReqStatus StdoutProbe::in_req(vp::Block *__this, vp::IoReq *req)
{
    StdoutProbe *_this = (StdoutProbe *)__this;

    // Read before forwarding: a slave acknowledging the write may rewrite
    // the request in place (io_v2_write_ack clears the data pointer)
    bool is_char = req->get_opcode() == vp::WRITE && req->get_addr() == _this->addr
        && req->get_data() != NULL;
    char c = is_char ? (char)req->get_data()[0] : 0;

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Request (addr: 0x%lx, size: 0x%lx, opcode: %d, "
        "data: %p)\n", req->get_addr(), req->get_size(), req->get_opcode(), req->get_data());

    vp::IoReqStatus status = _this->out.req(req);

    // Taken when the slave takes it, as the testbench sees the AW and W
    // handshakes: a denied write is seen again when the master retries it
    if (is_char && status != vp::IO_REQ_DENIED)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Character (value: 0x%x)\n", (uint8_t)c);
        _this->line += c;
        if (c == '\n')
        {
            fwrite(_this->line.data(), 1, _this->line.size(), stdout);
            _this->line.clear();
        }
    }

    return status;
}

void StdoutProbe::in_resp_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
    StdoutProbe *_this = (StdoutProbe *)__this;
    if (_this->out.is_resp_retry_bound())
    {
        _this->out.resp_retry(channel);
    }
}

vp::IoRespAck StdoutProbe::out_resp(vp::Block *__this, vp::IoReq *req)
{
    StdoutProbe *_this = (StdoutProbe *)__this;
    return _this->in.resp(req);
}

void StdoutProbe::out_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
    StdoutProbe *_this = (StdoutProbe *)__this;
    _this->in.retry(channel);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new StdoutProbe(config);
}
