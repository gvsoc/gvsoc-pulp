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
#include <pulp/snitch/snitch_cluster/spatz/cluster_periph_regfields.h>
#include <pulp/snitch/snitch_cluster/spatz/cluster_periph_gvsoc.h>


using namespace std::placeholders;


class ClusterRegisters : public vp::Component
{

public:

    ClusterRegisters(vp::ComponentConf &config);

    void reset(bool active);


private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static vp::IoReqStatus core_req(vp::Block *__this, vp::IoReq *req, int id);
    static void barrier_sync(vp::Block *__this, bool value, int id);
    void cl_clint_set_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void cl_clint_clear_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void hw_barrier_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);

    vp::Trace     trace;

    vp_regmap_cluster_periph regmap;

    vp::IoSlave in;
    std::vector<vp::IoSlave> cores_in;
    uint32_t bootaddr;
    uint32_t status;
    int nb_cores;
    vp::reg_32 barrier_status;

    std::vector<vp::WireSlave<bool>> barrier_req_itf;
    vp::WireMaster<bool> barrier_ack_itf;

    std::vector<vp::WireMaster<bool>> external_irq_itf;

    int core_access;
    bool stall_core;
    uint32_t waiting_cores;

    std::vector<vp::IoReq *> waiting_reqs;
};

ClusterRegisters::ClusterRegisters(vp::ComponentConf &config)
: vp::Component(config), regmap(*this, "regmap")
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->bootaddr = this->get_js_config()->get("boot_addr")->get_int();
    this->nb_cores = this->get_js_config()->get("nb_cores")->get_int();

    this->in.set_req_meth(&ClusterRegisters::req);
    this->new_slave_port("input", &this->in);

    this->cores_in.resize(this->nb_cores);
    this->waiting_reqs.resize(this->nb_cores);

    for (int i=0; i<this->nb_cores; i++)
    {
        this->cores_in[i].set_req_meth_muxed(&ClusterRegisters::core_req, i);
        this->new_slave_port("input_" + std::to_string(i), &this->cores_in[i]);
    }

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

    this->regmap.build(this, &this->trace, "regmap");
    this->regmap.cl_clint_set.register_callback(std::bind(&ClusterRegisters::cl_clint_set_req, this, _1, _2, _3, _4));
    this->regmap.cl_clint_clear.register_callback(std::bind(&ClusterRegisters::cl_clint_clear_req, this, _1, _2, _3, _4));
    this->regmap.hw_barrier.register_callback(std::bind(&ClusterRegisters::hw_barrier_req, this, _1, _2, _3, _4));
}

vp::IoReqStatus ClusterRegisters::core_req(vp::Block *__this, vp::IoReq *req, int id)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();

    _this->core_access = id;

    _this->trace.msg("Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n", offset, size, is_write);

    _this->regmap.access(offset, size, data, is_write);

    if (_this->stall_core)
    {
        _this->waiting_reqs[id] = req;
        _this->stall_core = false;
        return vp::IO_REQ_PENDING;
    }
    else
    {
        return vp::IO_REQ_OK;
    }
}

vp::IoReqStatus ClusterRegisters::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();

    _this->core_access = -1;

    _this->trace.msg("Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n", offset, size, is_write);

    _this->regmap.access(offset, size, data, is_write);

    return vp::IO_REQ_OK;
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

    if (!active)
    {
        this->waiting_cores = 0;
        this->stall_core = false;
    }
}


void ClusterRegisters::cl_clint_set_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    this->regmap.cl_clint_set.update(reg_offset, size, value, is_write);
    for (int i=0; i<this->nb_cores; i++)
    {
        int irq_status = (this->regmap.cl_clint_set.get() >> i) & 1;
        if (irq_status == 1)
        {
            this->external_irq_itf[i].sync(true);
        }
    }
}

void ClusterRegisters::hw_barrier_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    if (this->core_access != -1)
    {
        this->barrier_status.set(this->barrier_status.get() | (1 << this->core_access));

        if (this->barrier_status.get() == (1ULL << this->nb_cores) - 1)
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier reached\n");

            this->barrier_status.set(0);

            for (int i=0; i<this->nb_cores; i++)
            {
                if ((this->waiting_cores >> i) & 1)
                {
                    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Wakeup core waiting on barrier (core: %d)\n",
                        i);
                    this->waiting_reqs[i]->get_resp_port()->resp(this->waiting_reqs[i]);
                }
            }

            this->waiting_cores = 0;
        }
        else
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Stall core due to barrier not reached (core: %d)\n",
                this->core_access);

            this->waiting_cores |= 1 << this->core_access;
            this->stall_core = true;
            return;
        }
    }

    this->stall_core = false;
    return;
}

void ClusterRegisters::cl_clint_clear_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    this->regmap.cl_clint_clear.update(reg_offset, size, value, is_write);
    for (int i=0; i<this->nb_cores; i++)
    {
        int irq_status = (this->regmap.cl_clint_clear.get() >> i) & 1;
        if (irq_status == 1)
        {
            this->external_irq_itf[i].sync(false);
        }
    }
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterRegisters(config);
}
