/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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

#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <iostream>
#include <string>


using namespace std::placeholders;


void processChar(char c) {
    static std::string buffer;  // persists across function calls

    if (c == '\n') {
        std::cout << buffer << std::endl;
        buffer.clear();
    } else {
        buffer += c;
    }
}


class ErrorDetector : public vp::Component
{

public:

    ErrorDetector(vp::ComponentConf &config);
    void reset(bool active);
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);


private:
    vp::Trace       trace;
    vp::IoSlave     in;

};

ErrorDetector::ErrorDetector(vp::ComponentConf &config)
: vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
    this->in.set_req_meth(&ErrorDetector::req);
    this->new_slave_port("input", &this->in);
}

vp::IoReqStatus ErrorDetector::req(vp::Block *__this, vp::IoReq *req)
{
    ErrorDetector *_this = (ErrorDetector *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint32_t *data = (uint32_t *) req->get_data();

    _this->trace.fatal("[ErrorDetector] INVALID address: 0x%x\n", offset);

    _this->time.get_engine()->quit(0);

    return vp::IO_REQ_OK;
}


void ErrorDetector::reset(bool active)
{

}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ErrorDetector(config);
}


