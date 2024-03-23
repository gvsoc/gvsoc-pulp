/*
 * Copyright (C) 2024 ETH Zurich and University of Bologna
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
 * Authors: Germain Haugou, ETH Zurich (germain.haugou@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <string.h>
#include <pulp/chips/occamy/soc_reg_regfields.h>
#include <pulp/chips/occamy/soc_reg_gvsoc.h>


using namespace std::placeholders;


class SocReg : public vp::Component
{

public:
    SocReg(vp::ComponentConf &config);

    void reset(bool active);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    vp::IoSlave input_itf;

    vp_regmap_soc_reg regmap;
};



SocReg::SocReg(vp::ComponentConf &config)
    : vp::Component(config), regmap(*this, "regmap")
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->input_itf.set_req_meth(&SocReg::req);
    this->new_slave_port("input", &this->input_itf);

    this->regmap.build(this, &this->trace, "regmap");
}


void SocReg::reset(bool active)
{
}


vp::IoReqStatus SocReg::req(vp::Block *__this, vp::IoReq *req)
{
    SocReg *_this = (SocReg *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    _this->trace.msg("Soc register access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

    _this->regmap.access(offset, size, data, is_write);

    return vp::IO_REQ_OK;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new SocReg(config);
}
