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
 *
 * Performance counters (current map only, snitch_cluster_peripheral.sv): each
 * counter adds up the metric it selects, for the hart (or DMA channel) it
 * selects, while enabled. The cores and the DMA keep totals of their events
 * since reset; a counter catches up with the total of its source whenever it
 * is accessed, enabled, disabled or given another metric, so nothing is done
 * while it runs. The metrics with no source in the model (TCDM, FPU issue,
 * retired load / base / offloaded, DMA stalls and busy, instruction cache)
 * stay at 0.
 */

#include <algorithm>
#include <cstring>
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
#include <pulp/snitch/snitch_cluster/snitch_perf_events.hpp>

// Performance counter registers of the current map: enables, selections and
// counters, NumPerfCounters 64-bit registers each
#define PERF_NB_COUNTERS 16
#define PERF_CNT_EN_OFFSET  0x000
#define PERF_CNT_SEL_OFFSET 0x080
#define PERF_CNT_OFFSET     0x100
#define PERF_END_OFFSET     0x180
#define PERF_CNT_MASK       ((1ULL << 48) - 1)


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
#ifndef CLUSTER_PERIPH_OLD_REGMAP
    void perf_access(uint64_t offset, int size, uint8_t *data, bool is_write);
    void perf_reg_access(int reg, int offset, int size, uint8_t *data, bool is_write);
    // Total since reset of what counter @p id counts, for its current metric
    // and hart
    uint64_t perf_source(int id);
    // Brings counter @p id up to date with its source
    void perf_catch_up(int id);

    struct PerfCounter
    {
        uint64_t value;
        // Source total the value is up to date with
        uint64_t base;
        bool enable;
        int metric;
        int hart;
    };
    PerfCounter perf[PERF_NB_COUNTERS];
    // Bits of the hart selection the RTL keeps ($clog2(NrCores))
    int perf_hart_mask;
    std::vector<vp::WireMaster<SnitchPerfEvents *>> core_perf_events_itf;
    vp::WireMaster<SnitchPerfEvents *> dma_perf_events_itf;
#endif

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

#ifndef CLUSTER_PERIPH_OLD_REGMAP
    int hart_bits = 0;
    while ((1 << hart_bits) < this->nb_cores) hart_bits++;
    this->perf_hart_mask = (1 << hart_bits) - 1;
    this->core_perf_events_itf.resize(this->nb_cores);
    for (int i=0; i<this->nb_cores; i++)
    {
        this->new_master_port("core_perf_events_" + std::to_string(i), &this->core_perf_events_itf[i]);
    }
    this->new_master_port("dma_perf_events", &this->dma_perf_events_itf);
#endif
}

vp::IoReqStatus ClusterRegisters::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();

    _this->trace.msg("Received IO req (offset: 0x%lx, size: 0x%lx, is_write: %d)\n", offset, size, is_write);

#ifndef CLUSTER_PERIPH_OLD_REGMAP
    if (offset < PERF_END_OFFSET)
    {
        _this->perf_access(offset, size, data, is_write);
        req->set_resp_status(vp::IO_RESP_OK);
        return vp::IO_REQ_DONE;
    }
#endif

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

#ifndef CLUSTER_PERIPH_OLD_REGMAP
    if (!active)
    {
        // RTL reset: all counters enabled, the first ones tracking these
        // metrics of hart 0, the others the cycles
        static const int rst_metrics[] = {
            SNITCH_PERF_CYCLE, SNITCH_PERF_RETIRED_INSTR, SNITCH_PERF_TCDM_ACCESSED,
            SNITCH_PERF_ICACHE_MISS, SNITCH_PERF_ICACHE_HIT, SNITCH_PERF_ICACHE_PREFETCH,
            SNITCH_PERF_ICACHE_STALL,
        };
        for (int i=0; i<PERF_NB_COUNTERS; i++)
        {
            PerfCounter &cnt = this->perf[i];
            cnt.value = 0;
            cnt.enable = true;
            cnt.hart = 0;
            cnt.metric = i < (int)(sizeof(rst_metrics) / sizeof(rst_metrics[0])) ?
                rst_metrics[i] : SNITCH_PERF_CYCLE;
            cnt.base = this->perf_source(i);
        }
    }
#endif

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


// The CLINT set and clear registers are write pulses in the RTL: only the bits
// of the write act, and nothing is kept. A 64-bit write split into two 32-bit
// ones thus does nothing with its upper half.
void ClusterRegisters::cl_clint_set_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    this->regmap.cl_clint_set.update(reg_offset, size, value, is_write);
    if (!is_write) return;
    for (int i=0; i<this->nb_cores; i++)
    {
        int irq_status = (this->regmap.cl_clint_set.get() >> i) & 1;
        if (irq_status == 1)
        {
            this->external_irq_itf[i].sync(true);
        }
    }
    this->regmap.cl_clint_set.set(0);
}

void ClusterRegisters::cl_clint_clear_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    this->regmap.cl_clint_clear.update(reg_offset, size, value, is_write);
    if (!is_write) return;
    for (int i=0; i<this->nb_cores; i++)
    {
        int irq_status = (this->regmap.cl_clint_clear.get() >> i) & 1;
        if (irq_status == 1)
        {
            this->external_irq_itf[i].sync(false);
        }
    }
    this->regmap.cl_clint_clear.set(0);
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


#ifndef CLUSTER_PERIPH_OLD_REGMAP
uint64_t ClusterRegisters::perf_source(int id)
{
    PerfCounter &cnt = this->perf[id];
    int64_t cycles = this->clock.get_cycles();

    switch (cnt.metric)
    {
        case SNITCH_PERF_CYCLE:
            return cycles;
        case SNITCH_PERF_DMA_R_BW:
            // The RTL adds the data width every cycle, whatever the R channel does
            return cycles * this->cfg.dma_data_width;
    }

    if (cnt.metric >= SNITCH_PERF_NB_METRICS)
    {
        return 0;
    }

    bool is_dma = cnt.metric >= SNITCH_PERF_DMA_AW_STALL && cnt.metric <= SNITCH_PERF_DMA_BUSY;
    vp::WireMaster<SnitchPerfEvents *> *itf;
    if (is_dma)
    {
        // One DMA channel: the other harts select nothing
        itf = cnt.hart == 0 ? &this->dma_perf_events_itf : nullptr;
    }
    else
    {
        itf = cnt.hart < this->nb_cores ? &this->core_perf_events_itf[cnt.hart] : nullptr;
    }

    if (itf == nullptr || !itf->is_bound())
    {
        return 0;
    }

    SnitchPerfEvents events = {};
    itf->sync(&events);
    return events.count[cnt.metric];
}

void ClusterRegisters::perf_catch_up(int id)
{
    PerfCounter &cnt = this->perf[id];
    uint64_t source = this->perf_source(id);
    if (cnt.enable)
    {
        cnt.value = (cnt.value + source - cnt.base) & PERF_CNT_MASK;
    }
    cnt.base = source;
}

void ClusterRegisters::perf_access(uint64_t offset, int size, uint8_t *data, bool is_write)
{
    // The 64-bit registers may be accessed in smaller pieces
    while (size > 0)
    {
        int reg = offset / 8;
        int reg_offset = offset % 8;
        int chunk = std::min(size, 8 - reg_offset);
        this->perf_reg_access(reg, reg_offset, chunk, data, is_write);
        offset += chunk;
        data += chunk;
        size -= chunk;
    }
}

void ClusterRegisters::perf_reg_access(int reg, int offset, int size, uint8_t *data, bool is_write)
{
    int kind = reg / PERF_NB_COUNTERS;
    int id = reg % PERF_NB_COUNTERS;
    PerfCounter &cnt = this->perf[id];

    this->perf_catch_up(id);

    uint64_t value;
    switch (kind)
    {
        case 0: value = cnt.enable; break;
        case 1: value = (uint64_t)cnt.hart | ((uint64_t)cnt.metric << 16); break;
        default: value = cnt.value; break;
    }

    if (!is_write)
    {
        memcpy(data, (uint8_t *)&value + offset, size);
        return;
    }

    uint64_t mask = (size == 8 ? ~0ULL : ((1ULL << (size * 8)) - 1)) << (offset * 8);
    uint64_t wdata = 0;
    memcpy((uint8_t *)&wdata + offset, data, size);

    switch (kind)
    {
        case 0:
            if (mask & 1)
            {
                cnt.enable = wdata & 1;
            }
            break;
        case 1:
            // Each field only changes when some of its bits are written
            if (mask & 0xFFFF)
            {
                cnt.hart = wdata & this->perf_hart_mask;
            }
            if (mask & 0xFFFF0000)
            {
                cnt.metric = (wdata >> 16) & 0xFFFF;
            }
            // Count the new selection from now on
            cnt.base = this->perf_source(id);
            break;
        default:
            // Any write clears the counter
            cnt.value = 0;
            break;
    }

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Performance counter %d (enable: %d, metric: %d, "
        "hart: %d, value: %ld)\n", id, cnt.enable, cnt.metric, cnt.hart, cnt.value);
}
#endif


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterRegisters(config);
}
