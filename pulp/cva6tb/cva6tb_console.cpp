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

// Simulation console of the CVA6 standalone testbench (test/hw/cva6tb_sim_console.sv)

#include <stdio.h>
#include <string.h>
#include <string>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>

class Cva6tbConsole : public vp::Component
{
public:
    Cva6tbConsole(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::IoSlave input_itf{&Cva6tbConsole::req};
    std::string buffer;
};

Cva6tbConsole::Cva6tbConsole(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->new_slave_port("input", &this->input_itf);
}

vp::IoReqStatus Cva6tbConsole::req(vp::Block *__this, vp::IoReq *req)
{
    Cva6tbConsole *_this = (Cva6tbConsole *)__this;

    if (req->get_is_write())
    {
        // The RTL takes the low byte of a write to offset 0 (address bits 4:0)
        if ((req->get_addr() & 0x1f) == 0)
        {
            char c = req->get_data()[0];
            if (c == '\n')
            {
                printf("%s\n", _this->buffer.c_str());
                fflush(stdout);
                _this->buffer.clear();
            }
            else
            {
                _this->buffer += c;
            }
        }
    }
    else
    {
        memset(req->get_data(), 0, req->get_size());
    }

    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Cva6tbConsole(config);
}
