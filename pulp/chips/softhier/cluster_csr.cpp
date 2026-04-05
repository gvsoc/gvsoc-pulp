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


class ClusterCSR : public vp::Component
{

public:

    ClusterCSR(vp::ComponentConf &config);
    void reset(bool active);
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);


private:
    static void barrier_sync(vp::Block *__this, bool value, int id);

    vp::Trace       trace;
    vp::IoSlave     in;
    int             nb_cores;
    uint32_t        cluster_id;
    vp::reg_32      barrier_status;
    std::string     buffer;

    std::vector<vp::WireSlave<bool>> barrier_req_itf;
    vp::WireMaster<bool> barrier_ack_itf;
    std::vector<vp::WireMaster<bool>> external_irq_itf;
};

ClusterCSR::ClusterCSR(vp::ComponentConf &config)
: vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->in.set_req_meth(&ClusterCSR::req);
    this->new_slave_port("input", &this->in);
    this->nb_cores = this->get_js_config()->get("nb_cores")->get_int();
    this->cluster_id = this->get_js_config()->get("cluster_id")->get_int();
    this->barrier_req_itf.resize(this->nb_cores);
    for (int i=0; i<this->nb_cores; i++)
    {
        this->barrier_req_itf[i].set_sync_meth_muxed(&ClusterCSR::barrier_sync, i);
        this->new_slave_port("barrier_req_" + std::to_string(i), &this->barrier_req_itf[i]);
    }

    this->external_irq_itf.resize(this->nb_cores);
    for (int i=0; i<this->nb_cores; i++)
    {
        this->new_master_port("external_irq_" + std::to_string(i), &this->external_irq_itf[i]);
    }

    this->new_master_port("barrier_ack", &this->barrier_ack_itf);
}

vp::IoReqStatus ClusterCSR::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterCSR *_this = (ClusterCSR *)__this;
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
        data[0] = 0;
    }

    if(offset == 12){
        uint32_t value = *(uint32_t *)data;
        char c = (char)value;
        if (c == '\n') {
            std::cout << _this->buffer << std::endl;
            _this->buffer.clear();
        } else {
            _this->buffer += c;
        }
    }

    return vp::IO_REQ_OK;
}

void ClusterCSR::barrier_sync(vp::Block *__this, bool value, int id)
{
    ClusterCSR *_this = (ClusterCSR *)__this;
    _this->barrier_status.set(_this->barrier_status.get() | (value << id));

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier sync (id: %d, status: 0x%x)\n", id, _this->barrier_status.get());

    if (_this->barrier_status.get() == (1ULL << _this->nb_cores) - 1)
    {
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier reached\n");

        _this->barrier_status.set(0);
        _this->barrier_ack_itf.sync(1);
    }
}

void ClusterCSR::reset(bool active)
{
    this->new_reg("barrier_status", &this->barrier_status, 0, true);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterCSR(config);
}


