// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

/*
 * HyperBus pads of a chip: the HyperBus of the uDMA goes to the device of the
 * active chip select, and its data come back. This is the HyperBus part of
 * the io (v1) padframe (pulp/padframe/padframe_v1_impl.cpp), whose chip
 * select is active low on the device side.
 */

#include <vp/vp.hpp>
#include <vp/itf/hyper.hpp>
#include <ips/pulp/hyper_cs/hyper_cs/hyper_cs_config.hpp>


class HyperCs : public vp::Component
{
public:
    HyperCs(vp::ComponentConf &config);

private:
    static void hyper_sync_cycle(vp::Block *__this, int data);
    static void hyper_cs_sync(vp::Block *__this, int cs, int active);
    static void device_sync_cycle(vp::Block *__this, int data, int cs);

    HyperCsConfig cfg;
    vp::Trace trace;
    // The HyperBus of the uDMA
    vp::HyperSlave in;
    // One per chip select
    std::vector<vp::HyperMaster> out;
    int active_cs;
    vp::Trace data_trace;
    std::vector<vp::Trace> cs_trace;
};


HyperCs::HyperCs(vp::ComponentConf &config)
    : vp::Component(config, this->cfg), out(this->cfg.nb_cs), cs_trace(this->cfg.nb_cs)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->traces.new_trace_event("data", &this->data_trace, 8);

    this->in.set_sync_cycle_meth(&HyperCs::hyper_sync_cycle);
    this->in.set_cs_sync_meth(&HyperCs::hyper_cs_sync);
    this->new_slave_port("input", &this->in);

    for (int i = 0; i < this->cfg.nb_cs; i++)
    {
        this->traces.new_trace_event("cs_" + std::to_string(i), &this->cs_trace[i], 1);
        this->out[i].set_sync_cycle_meth_muxed(&HyperCs::device_sync_cycle, i);
        this->new_master_port("cs" + std::to_string(i), &this->out[i]);
    }

    this->active_cs = 0;
}


void HyperCs::hyper_sync_cycle(vp::Block *__this, int data)
{
    HyperCs *_this = (HyperCs *)__this;
    _this->data_trace.event((uint8_t *)&data);
    if (!_this->out[_this->active_cs].is_bound())
    {
        vp_warning_always(&_this->trace, "Trying to send HYPER stream while pad is not connected (cs: %d)\n",
            _this->active_cs);
    }
    else
    {
        _this->out[_this->active_cs].sync_cycle(data);
    }
}


void HyperCs::hyper_cs_sync(vp::Block *__this, int cs, int active)
{
    HyperCs *_this = (HyperCs *)__this;

    if (cs >= _this->cfg.nb_cs)
    {
        vp_warning_always(&_this->trace, "Trying to activate invalid cs (cs: %d, nb_cs: %d)\n",
            cs, _this->cfg.nb_cs);
        return;
    }

    _this->cs_trace[cs].event((uint8_t *)&active);
    _this->active_cs = cs;

    if (!_this->out[cs].is_bound())
    {
        vp_warning_always(&_this->trace, "Trying to send HYPER stream while pad is not connected (cs: %d)\n", cs);
    }
    else
    {
        _this->out[cs].cs_sync(cs, !active);
    }
}


void HyperCs::device_sync_cycle(vp::Block *__this, int data, int cs)
{
    HyperCs *_this = (HyperCs *)__this;
    _this->data_trace.event((uint8_t *)&data);
    _this->in.sync_cycle(data);
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new HyperCs(config);
}
