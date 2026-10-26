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
#include <vector>

// Register offsets, the same as the SoftHier control registers up to 20
#define MC_POOL_CTRL_EOC            0
#define MC_POOL_CTRL_TIMER_START    8
#define MC_POOL_CTRL_TIMER_PRINT    12
#define MC_POOL_CTRL_PRINT_CHAR     16
#define MC_POOL_CTRL_PRINT_INT      20
// End of computation of one cluster, as (cluster_id << 16) | value
#define MC_POOL_CTRL_CLUSTER_EOC    24
// Stops the simulation after the given number of cycles
#define MC_POOL_CTRL_DEBUG_STOP     40

class McPoolCtrl : public vp::Component
{

public:
    McPoolCtrl(vp::ComponentConf &config);
    ~McPoolCtrl();

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void debug_stop_event_handler(vp::Block *__this, vp::ClockEvent *event);
    static void hbm_preload_done_handler(vp::Block *__this, bool value);
    static void hbm_preload_done_to_cluster_handler(vp::Block *__this, vp::ClockEvent *event);
    void reset(bool active);

    vp::Trace               trace;
    vp::IoSlave             input_itf;
    vp::WireSlave<bool>     hbm_preload_done_itf;
    vp::WireMaster<bool> *  hbm_preload_done_to_cluster_itf_array;
    vp::ClockEvent *        debug_stop_event;
    vp::ClockEvent *        hbm_preload_done_to_cluster_event;
    uint32_t                num_cluster_x;
    uint32_t                num_cluster_y;
    uint32_t                num_cluster;
    uint32_t                has_preload_binary;
    uint32_t                hbm_preload_done;
    int64_t                 timer_start;
    uint32_t                cluster_eoc_count;
    std::vector<uint8_t>    cluster_eoc_seen;
};

McPoolCtrl::McPoolCtrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->num_cluster_x = this->get_js_config()->get("num_cluster_x")->get_int();
    this->num_cluster_y = this->get_js_config()->get("num_cluster_y")->get_int();
    this->has_preload_binary = this->get_js_config()->get("has_preload_binary")->get_int();
    this->num_cluster = this->num_cluster_x * this->num_cluster_y;
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&McPoolCtrl::req);
    this->new_slave_port("input", &this->input_itf);

    // The clusters are started once the HBM is preloaded
    this->hbm_preload_done_itf.set_sync_meth(&McPoolCtrl::hbm_preload_done_handler);
    this->new_slave_port("hbm_preload_done", &this->hbm_preload_done_itf);
    this->hbm_preload_done_to_cluster_itf_array = new vp::WireMaster<bool>[this->num_cluster];
    for (int i = 0; i < this->num_cluster; ++i)
    {
        this->new_master_port("hbm_preload_done_to_cluster_"  + std::to_string(i),
            &(this->hbm_preload_done_to_cluster_itf_array[i]));
    }

    this->debug_stop_event = this->event_new(&McPoolCtrl::debug_stop_event_handler);
    this->hbm_preload_done_to_cluster_event = this->event_new(
        &McPoolCtrl::hbm_preload_done_to_cluster_handler);
    this->hbm_preload_done = (this->has_preload_binary == 0)? 1:0;
    this->timer_start = 0;
    this->cluster_eoc_count = 0;
    this->cluster_eoc_seen.assign(this->num_cluster, 0);
}

McPoolCtrl::~McPoolCtrl()
{
    delete[] this->hbm_preload_done_to_cluster_itf_array;
}

void McPoolCtrl::reset(bool active)
{
    if (active)
    {
        std::cout << "[SystemInfo]: num_cluster_x = " << this->num_cluster_x << ", num_cluster_y = " << this->num_cluster_y << std::endl;
        this->cluster_eoc_count = 0;
        this->cluster_eoc_seen.assign(this->num_cluster, 0);
        this->event_enqueue(this->hbm_preload_done_to_cluster_event, 300);
    }
}

void McPoolCtrl::debug_stop_event_handler(vp::Block *__this, vp::ClockEvent *event)
{
    McPoolCtrl *_this = (McPoolCtrl *)__this;
    std::cout << "[Debug Stop]: Stop simulation at cycle: " << _this->clock.get_cycles() << std::endl;
    _this->time.get_engine()->quit(1);
}

void McPoolCtrl::hbm_preload_done_to_cluster_handler(vp::Block *__this, vp::ClockEvent *event)
{
    McPoolCtrl *_this = (McPoolCtrl *)__this;
    if (_this->hbm_preload_done)
    {
        _this->trace.msg("HBM Preload Done, Acknowledge all Clusters\n");
        for (int i = 0; i < _this->num_cluster; ++i)
        {
            _this->hbm_preload_done_to_cluster_itf_array[i].sync(1);
        }
    }
    else
    {
        _this->event_enqueue(_this->hbm_preload_done_to_cluster_event, 300);
    }
}

void McPoolCtrl::hbm_preload_done_handler(vp::Block *__this, bool value)
{
    McPoolCtrl *_this = (McPoolCtrl *)__this;
    _this->hbm_preload_done = 1;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "HBM Preloading Done\n");
}

vp::IoReqStatus McPoolCtrl::req(vp::Block *__this, vp::IoReq *req)
{
    McPoolCtrl *_this = (McPoolCtrl *)__this;
    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if (is_write && size == 4)
    {
        uint32_t value = *(uint32_t *)data;
        if (offset == MC_POOL_CTRL_EOC)
        {
            _this->time.get_engine()->quit(0);
        }
        if (offset == MC_POOL_CTRL_TIMER_START)
        {
            _this->timer_start = _this->time.get_time();
        }
        if (offset == MC_POOL_CTRL_TIMER_PRINT)
        {
            int64_t period = _this->time.get_time() - _this->timer_start;
            std::cout << "[Performance Counter]: Execution period is " << period/1000 << " ns" << std::endl;
            _this->timer_start = _this->time.get_time();
        }
        if (offset == MC_POOL_CTRL_PRINT_CHAR)
        {
            char c = (char)value;
            std::cout << c;
        }
        if (offset == MC_POOL_CTRL_PRINT_INT)
        {
            std::cout << value;
        }
        if (offset == MC_POOL_CTRL_CLUSTER_EOC)
        {
            // The simulation ends once every cluster has reached its end of computation
            uint32_t cluster_id = (value >> 16) & 0xFFFF;
            uint32_t eoc_value = value & 0xFFFF;

            if (cluster_id >= _this->num_cluster)
            {
                _this->trace.msg("Ignoring invalid cluster EOC notification (cluster_id: %d, value: 0x%x)\n",
                    cluster_id, value);
            }
            else if (_this->cluster_eoc_seen[cluster_id])
            {
                _this->trace.msg("Ignoring repeated EOC for cluster %d (value: 0x%x)\n",
                    cluster_id, eoc_value);
            }
            else
            {
                _this->cluster_eoc_seen[cluster_id] = 1;
                _this->cluster_eoc_count++;
                _this->trace.msg("Cluster %d EOC received (value: 0x%x, hartid: %d)\n",
                    cluster_id, eoc_value, eoc_value >> 1);
                if (_this->cluster_eoc_count == _this->num_cluster)
                {
                    _this->trace.msg("All clusters reached EOC, terminating simulation with code 0\n");
                    _this->time.get_engine()->quit(0);
                }
            }
        }
        if (offset == MC_POOL_CTRL_DEBUG_STOP)
        {
            _this->event_enqueue(_this->debug_stop_event, value);
        }
    }

    return vp::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new McPoolCtrl(config);
}
