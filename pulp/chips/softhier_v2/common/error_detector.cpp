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

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>

/**
 * Catches the accesses no other SoftHier target claimed and stops the
 * simulation with an error.
 */
class ErrorDetector : public vp::Component
{
public:
    ErrorDetector(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    vp::IoSlave in{&ErrorDetector::req};
};

ErrorDetector::ErrorDetector(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->in);
}

vp::IoReqStatus ErrorDetector::req(vp::Block *__this, vp::IoReq *req)
{
    ErrorDetector *_this = (ErrorDetector *)__this;
    _this->trace.fatal("[ErrorDetector] INVALID address: 0x%lx\n", req->get_addr());
    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ErrorDetector(config);
}
