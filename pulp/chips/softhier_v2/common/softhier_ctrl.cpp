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
#include <iostream>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <pulp/chips/softhier_v2/common/softhier_ctrl/soft_hier_ctrl_config.hpp>

/**
 * SoftHier SoC control registers (32-bit writes):
 * - 0x0:  end of computation, stops the simulation
 * - 0x4:  one core reached its end of computation, with its status; the
 *         simulation stops once every core of every cluster did, with the OR of
 *         the statuses as exit status. The core then parks itself (the
 *         runtime returns to a WFI loop). Unlike v1, the write is answered: an
 *         io_v2 write left pending would hold the crossbar outputs it went
 *         through and block the other cores of the cluster.
 * - 0x8:  start the performance timer
 * - 0xC:  print the time elapsed since the timer start, and restart it
 * - 0x10: character output
 * - 0x14: integer output
 */
class SoftHierCtrl : public vp::Component
{
public:
    SoftHierCtrl(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    void reset(bool active) override;

    SoftHierCtrlConfig cfg;
    vp::Trace trace;
    vp::IoSlave input_itf{&SoftHierCtrl::req};
    int64_t timer_start;
    int64_t all_eoc_counter;
    // OR of the values reported with the per-core end of computation, used as
    // the simulation exit status
    uint32_t all_eoc_status;
};

SoftHierCtrl::SoftHierCtrl(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->input_itf);
}

void SoftHierCtrl::reset(bool active)
{
    if (active)
    {
        this->timer_start = 0;
        this->all_eoc_counter = 0;
        this->all_eoc_status = 0;
        std::cout << "[SystemInfo]: num_cluster = " << this->cfg.num_cluster << std::endl;
    }
}

vp::IoReqStatus SoftHierCtrl::req(vp::Block *__this, vp::IoReq *req)
{
    SoftHierCtrl *_this = (SoftHierCtrl *)__this;
    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Access (offset: 0x%lx, size: 0x%lx, is_write: %d)\n",
        offset, size, is_write);

    if (is_write && size == 4)
    {
        uint32_t value = *(uint32_t *)data;
        if (offset == 0)
        {
            _this->time.get_engine()->quit(0);
        }
        else if (offset == 4)
        {
            _this->all_eoc_counter += 1;
            _this->all_eoc_status |= value;
            if (_this->all_eoc_counter >= _this->cfg.num_cluster * _this->cfg.num_core_per_cluster)
            {
                _this->time.get_engine()->quit(_this->all_eoc_status);
            }
        }
        else if (offset == 8)
        {
            _this->timer_start = _this->time.get_time();
        }
        else if (offset == 12)
        {
            int64_t period = _this->time.get_time() - _this->timer_start;
            std::cout << "[Performance Counter]: Execution period is " << period / 1000 << " ns"
                << std::endl;
            _this->timer_start = _this->time.get_time();
        }
        else if (offset == 16)
        {
            std::cout << (char)value;
        }
        else if (offset == 20)
        {
            std::cout << value;
        }
    }
    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new SoftHierCtrl(config);
}
