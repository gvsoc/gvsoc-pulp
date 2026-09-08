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
 * io_v2 port of the Spatz cluster peripheral registers. Same regmap and
 * behaviour as cluster_registers.cpp; the IO plumbing follows io_v2:
 *   - normal accesses answer inline with IO_REQ_DONE (+latency annotation)
 *   - a core loading HW_BARRIER before the barrier is reached gets
 *     IO_REQ_GRANTED; the response is sent through the core's own slave
 *     port once the last core reaches the barrier.
 *
 * Barrier timing. In the RTL the barrier (spatz_barrier.sv) is a filter on
 * each core's data port, in front of the cluster crossbar: a load to
 * HW_BARRIER is held at the core's port until every core has one held, then
 * all of them are released together and each travels the normal path to the
 * peripheral and back (the register itself reads 0). So after the barrier is
 * taken every core still pays the full round trip to the peripheral, and the
 * cores released together serialise on the peripheral's request bus. Here the
 * held requests have already reached the peripheral, so on release they are
 * answered after the same latency the last arriver is charged, plus the
 * serialisation behind the requests released ahead of them (measured on the
 * Verilator RTL by tests/calibration/targets/spatz/barrier: last arriver 10
 * cycles from issue to retire, the waiting core 3 more).
 */

#include <deque>
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
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
    void cl_clint_set_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void cl_clint_clear_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void hw_barrier_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    static void release_handler(vp::Block *__this, vp::ClockEvent *event);
    void schedule_release();

    // Latency of a peripheral access as seen by the core (issue to retire),
    // charged inline on every access and again to the held barrier loads
    // once they are released.
    static constexpr int64_t ACCESS_LATENCY = 11;
    // Extra cycles per request released behind another one: the requests
    // released together serialise on the crossbar and the register bus.
    static constexpr int64_t RELEASE_SERIAL = 3;

    vp::Trace     trace;

    vp_regmap_cluster_periph regmap;

    vp::IoSlave in{&ClusterRegisters::req};
    std::vector<vp::IoSlave> cores_in;
    uint32_t bootaddr;
    int nb_cores;
    vp::reg_32 barrier_status;

    std::vector<vp::WireMaster<bool>> external_irq_itf;

    int core_access;
    bool stall_core;
    uint32_t waiting_cores;

    std::vector<vp::IoReq *> waiting_reqs;

    // Held barrier loads released, waiting for their round trip to elapse.
    struct PendingRelease
    {
        int core;
        vp::IoReq *req;
        int64_t due;
    };
    std::deque<PendingRelease> releases;
    vp::ClockEvent release_event;
};

ClusterRegisters::ClusterRegisters(vp::ComponentConf &config)
: vp::Component(config), regmap(*this, "regmap"),
  release_event(this, &ClusterRegisters::release_handler)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->bootaddr = this->get_js_config()->get("boot_addr")->get_int();
    this->nb_cores = this->get_js_config()->get("nb_cores")->get_int();

    this->new_slave_port("input", &this->in);

    this->cores_in.reserve(this->nb_cores);
    this->waiting_reqs.resize(this->nb_cores);

    for (int i=0; i<this->nb_cores; i++)
    {
        this->cores_in.emplace_back(i, &ClusterRegisters::core_req);
        this->new_slave_port("input_" + std::to_string(i), &this->cores_in[i]);
    }

    this->external_irq_itf.resize(this->nb_cores);
    for (int i=0; i<this->nb_cores; i++)
    {
        this->new_master_port("external_irq_" + std::to_string(i), &this->external_irq_itf[i]);
    }

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

    _this->trace.msg("Received IO req (offset: 0x%lx, size: 0x%lx, is_write: %d)\n", offset, size, is_write);

    _this->regmap.access(offset, size, data, is_write);

    // Round trip through the cluster crossbar and the register bus
    req->inc_latency(ACCESS_LATENCY);
    req->set_resp_status(vp::IO_RESP_OK);

    if (_this->stall_core)
    {
        // Held until the barrier is taken; answered from release_handler
        _this->waiting_reqs[id] = req;
        _this->stall_core = false;
        return vp::IO_REQ_GRANTED;
    }
    else
    {
        return vp::IO_REQ_DONE;
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

    _this->trace.msg("Received IO req (offset: 0x%lx, size: 0x%lx, is_write: %d)\n", offset, size, is_write);

    _this->regmap.access(offset, size, data, is_write);

    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
}

void ClusterRegisters::reset(bool active)
{
    this->new_reg("barrier_status", &this->barrier_status, 0, true);

    if (!active)
    {
        this->waiting_cores = 0;
        this->stall_core = false;
        this->releases.clear();
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

            // The last arriver's load is answered inline with ACCESS_LATENCY.
            // The held loads are released now and pay the same round trip,
            // each one RELEASE_SERIAL cycles behind the request released
            // before it (the last arriver's being the first).
            int64_t now = this->clock.get_cycles();
            int rank = 1;
            for (int i=0; i<this->nb_cores; i++)
            {
                if ((this->waiting_cores >> i) & 1)
                {
                    int64_t due = now + ACCESS_LATENCY + RELEASE_SERIAL * rank;
                    this->trace.msg(vp::Trace::LEVEL_DEBUG,
                        "Releasing core waiting on barrier (core: %d, due: %ld)\n", i, due);
                    this->releases.push_back({i, this->waiting_reqs[i], due});
                    this->waiting_reqs[i] = NULL;
                    rank++;
                }
            }

            this->waiting_cores = 0;
            this->schedule_release();
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

void ClusterRegisters::schedule_release()
{
    if (this->releases.empty() || this->release_event.is_enqueued())
    {
        return;
    }
    int64_t delay = this->releases.front().due - this->clock.get_cycles();
    this->release_event.enqueue(delay > 0 ? delay : 1);
}

void ClusterRegisters::release_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    int64_t now = _this->clock.get_cycles();

    while (!_this->releases.empty() && _this->releases.front().due <= now)
    {
        PendingRelease release = _this->releases.front();
        _this->releases.pop_front();
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Wakeup core waiting on barrier (core: %d)\n",
            release.core);
        _this->cores_in[release.core].resp(release.req);
    }

    _this->schedule_release();
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
