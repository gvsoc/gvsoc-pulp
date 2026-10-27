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

/*
 * io_v2 port of the Snitch cluster peripheral registers, with the regmap of
 * the current Snitch cluster RTL (snitch_cluster_peripheral_v2_reg.hjson), or
 * the older one of Occamy with CLUSTER_PERIPH_OLD_REGMAP, and the behaviour of cluster_registers.cpp. The accesses are answered inline
 * with IO_REQ_DONE.
 */

#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#ifdef CLUSTER_PERIPH_OLD_REGMAP
// Register map of the older Snitch clusters (Occamy)
#include <pulp/snitch/snitch_cluster/cluster_periph_regfields.h>
#include <pulp/snitch/snitch_cluster/cluster_periph_gvsoc.h>
typedef vp_regmap_cluster_periph cluster_periph_regmap_t;
#else
#include <pulp/snitch/snitch_cluster/cluster_periph_v2_regfields.h>
#include <pulp/snitch/snitch_cluster/cluster_periph_v2_gvsoc.h>
typedef vp_regmap_cluster_periph_v2 cluster_periph_regmap_t;
#endif
#include <pulp/snitch/snitch_cluster/cluster_registers_v2/cluster_registers_config.hpp>


using namespace std::placeholders;


class ClusterRegisters : public vp::Component
{

public:

    ClusterRegisters(vp::ComponentConf &config);

    void reset(bool active);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void barrier_sync(vp::Block *__this, bool value, int id);
    void cl_clint_set_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void cl_clint_clear_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void icache_prefetch_enable_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);

    ClusterRegistersConfig cfg;

    vp::Trace     trace;

    cluster_periph_regmap_t regmap;

    vp::IoSlave in{&ClusterRegisters::req};
    int nb_cores;
    vp::reg_32 barrier_status;

    std::vector<vp::WireSlave<bool>> barrier_req_itf;
    vp::WireMaster<bool> barrier_ack_itf;

    std::vector<vp::WireMaster<bool>> external_irq_itf;
    // icache_prefetch_enable_o, to the enable_prefetching_i of every L0
    vp::WireMaster<bool> icache_prefetch_enable_itf;
};

ClusterRegisters::ClusterRegisters(vp::ComponentConf &config)
: vp::Component(config, this->cfg), regmap(*this, "regmap")
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->new_slave_port("input", &this->in);

    this->nb_cores = this->cfg.nb_cores;

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

    this->new_reg("barrier_status", &this->barrier_status, 0, true);

    this->regmap.build(this, &this->trace, "regmap");
    this->regmap.cl_clint_set.register_callback(std::bind(&ClusterRegisters::cl_clint_set_req, this, _1, _2, _3, _4));
    this->regmap.cl_clint_clear.register_callback(std::bind(&ClusterRegisters::cl_clint_clear_req, this, _1, _2, _3, _4));
    this->regmap.icache_prefetch_enable.register_callback(std::bind(&ClusterRegisters::icache_prefetch_enable_req, this, _1, _2, _3, _4));
    this->new_master_port("icache_prefetch_enable", &this->icache_prefetch_enable_itf);
}

vp::IoReqStatus ClusterRegisters::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();

    _this->trace.msg("Received IO req (offset: 0x%lx, size: 0x%lx, is_write: %d)\n", offset, size, is_write);

    _this->regmap.access(offset, size, data, is_write);

    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
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
    if (active)
    {
        this->barrier_status.set(0);
    }

    if (!active)
    {
        // RTL reset value 1 (resval of the register, missed by the regmap
        // generator, which only takes the field ones)
        this->regmap.icache_prefetch_enable.set(1);
        if (this->icache_prefetch_enable_itf.is_bound())
        {
            this->icache_prefetch_enable_itf.sync(true);
        }
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

void ClusterRegisters::icache_prefetch_enable_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    this->regmap.icache_prefetch_enable.update(reg_offset, size, value, is_write);
    if (this->icache_prefetch_enable_itf.is_bound())
    {
        this->icache_prefetch_enable_itf.sync(
            this->regmap.icache_prefetch_enable.icache_prefetch_enable_get());
    }
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterRegisters(config);
}
