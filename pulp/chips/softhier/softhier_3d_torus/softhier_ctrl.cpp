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
            Yichao  Zhang , ETH Zurich (yiczhang@iis.ee.ethz.ch)
            Chi     Zhang , ETH Zurich (chizhang@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <iostream>

void printProgressBar(int progress, int total, int width = 100) {
    float ratio = (float)progress / total;
    int filled = (int)(ratio * width);

    std::cout << "\r[";
    for (int i = 0; i < width; ++i) {
        if (i < filled)
            std::cout << "=";
        else if (i == filled)
            std::cout << ">";
        else
            std::cout << " ";
    }

    std::cout << "] " << (int)(ratio * 100) << "%\n";
    std::cout.flush();
}


class SoftHierCtrl : public vp::Component
{

public:
    SoftHierCtrl(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    void reset(bool active);

    vp::Trace               trace;
    vp::IoSlave             input_itf;
    uint32_t                num_cluster;
    uint32_t                num_core_per_cluster;
    int64_t                 timer_start;
    int64_t                 all_eoc_conuter;
};



SoftHierCtrl::SoftHierCtrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->num_cluster = this->get_js_config()->get("num_cluster")->get_int();
    this->num_core_per_cluster = this->get_js_config()->get("num_core_per_cluster")->get_int();

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&SoftHierCtrl::req);
    this->new_slave_port("input", &this->input_itf);
    this->timer_start = 0;
    this->all_eoc_conuter = 0;
}

void SoftHierCtrl::reset(bool active)
{
    if (active)
    {
        std::cout << "[SystemInfo]: num_cluster = " << this->num_cluster << std::endl;
    }
}

vp::IoReqStatus SoftHierCtrl::req(vp::Block *__this, vp::IoReq *req)
{
    SoftHierCtrl *_this = (SoftHierCtrl *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if (is_write && size == 4)
    {
        uint32_t value = *(uint32_t *)data;
        if (offset == 0)
        {
            // std::cout << "EOC register return value: 0x" << std::hex << value << std::endl;
            _this->time.get_engine()->quit(0);
        }
        if (offset == 4)
        {
            _this->all_eoc_conuter += 1;
            // _this->trace.msg("Control registers access (offset: 0x%x, size: 0x%x, is_write: %d, data:%x)\n", offset, size, is_write, *(uint32_t *)data);
            // printProgressBar(_this->all_eoc_conuter, _this->num_cluster);
            if (_this->all_eoc_conuter >= (_this->num_cluster * _this->num_core_per_cluster))
            {
                _this->time.get_engine()->quit(0);
            }

            return vp::IO_REQ_PENDING;
        }
        if (offset == 8)
        {
            _this->timer_start = _this->time.get_time();
        }
        if (offset == 12)
        {
            int64_t period = _this->time.get_time() - _this->timer_start;
            std::cout << "[Performance Counter]: Execution period is " << period/1000 << " ns" << std::endl;
            _this->timer_start = _this->time.get_time();
        }
        if (offset == 16)
        {
            char c = (char)value;
            std::cout << c;
        }
        if (offset == 20)
        {
            std::cout << value;
        }
    }

    return vp::IO_REQ_OK;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new SoftHierCtrl(config);
}