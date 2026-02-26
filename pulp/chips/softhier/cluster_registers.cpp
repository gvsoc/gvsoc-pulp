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
#include <pulp/snitch/snitch_cluster/cluster_periph_regfields.h>
#include <pulp/snitch/snitch_cluster/cluster_periph_gvsoc.h>


using namespace std::placeholders;


class ClusterRegisters : public vp::Component
{

public:

    ClusterRegisters(vp::ComponentConf &config);
    void reset(bool active);
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);


private:
    static void barrier_sync(vp::Block *__this, bool value, int id);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);
    static void hbm_preload_done_handler(vp::Block *__this, bool value);

    vp::Trace       trace;
    vp::IoSlave     in;
    uint32_t        bootaddr;
    int             nb_cores;
    uint32_t        cluster_id;
    vp::reg_32      barrier_status;
    uint32_t        num_cluster_x;
    uint32_t        num_cluster_y;

    std::vector<vp::WireSlave<bool>> barrier_req_itf;
    vp::WireMaster<bool> barrier_ack_itf;

    vp::WireSlave<bool> hbm_preload_done_itf;
    vp::WireMaster<bool> fetch_start_itf;

    std::vector<vp::WireMaster<bool>> external_irq_itf;
};

ClusterRegisters::ClusterRegisters(vp::ComponentConf &config)
: vp::Component(config), regmap(*this, "regmap")
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->in.set_req_meth(&ClusterRegisters::req);
    this->new_slave_port("input", &this->in);

    this->bootaddr = this->get_js_config()->get("boot_addr")->get_int();
    this->nb_cores = this->get_js_config()->get("nb_cores")->get_int();
    this->cluster_id = this->get_js_config()->get("cluster_id")->get_int();
    this->num_cluster_x = this->get_js_config()->get("num_cluster_x")->get_int();
    this->num_cluster_y = this->get_js_config()->get("num_cluster_y")->get_int();

    this->barrier_req_itf.resize(this->nb_cores);
    for (int i=0; i<this->nb_cores; i++)
    {
        this->barrier_req_itf[i].set_sync_meth_muxed(&ClusterRegisters::barrier_sync, i);
        this->new_slave_port("barrier_req_" + std::to_string(i), &this->barrier_req_itf[i]);
    }

    this->external_irq_itf.resize(this->nb_cores);
    for (int i=0; i<this->nb_cores; i++)
    {
        this->new_master_port("external_irq_" + std::to_string(i), &this->external_irq_itf[i]);
    }

    this->new_master_port("barrier_ack", &this->barrier_ack_itf);

    this->new_slave_port("hbm_preload_done", &this->hbm_preload_done_itf);
    this->new_master_port("fetch_start", &this->fetch_start_itf);
    this->hbm_preload_done_itf.set_sync_meth(&ClusterRegisters::hbm_preload_done_handler);
}

vp::IoReqStatus ClusterRegisters::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint32_t *data = (uint32_t *) req->get_data();

    if (!is_write && offset == 0)
    {
        data[0] = _this->cluster_id;
    }

    if(offset == 4){
        data[0] = 1;
    }

    if(offset == 8){
        data[0] = _this->num_cluster_x * _this->num_cluster_y;
    }

    if(offset == 12){
        data[0] = _this->num_cluster_x;
    }

    if(offset == 16){
        data[0] = _this->num_cluster_y;
    }

    if(offset == 20){
        data[0] = 0;
    }

    if (is_write && offset == 24)
    {
        _this->time.get_engine()->quit(0);
    }

    return vp::IO_REQ_OK;
}

void ClusterRegisters::hbm_preload_done_handler(vp::Block *__this, bool value)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "HBM Preloading Done\n");
    _this->fetch_start_itf.sync(1);
}

void ClusterRegisters::barrier_sync(vp::Block *__this, bool value, int id)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    _this->barrier_status.set(_this->barrier_status.get() | (value << id));

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier sync (id: %d, status: 0x%x)\n", id, _this->barrier_status.get());

    if (_this->barrier_status.get() == (1ULL << _this->nb_cores) - 1)
    {
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier reached\n");

        _this->barrier_status.set(0);
        _this->barrier_ack_itf.sync(1);
    }
}

void ClusterRegisters::reset(bool active)
{
    this->new_reg("barrier_status", &this->barrier_status, 0, true);
}


void ClusterRegisters::response(vp::Block *__this, vp::IoReq *req)
{

}


void ClusterRegisters::grant(vp::Block *__this, vp::IoReq *req)
{

}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterRegisters(config);
}


