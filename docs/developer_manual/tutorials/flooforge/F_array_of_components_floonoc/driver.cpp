#include <vp/vp.hpp>
#include "driver.hpp"

Driver::Driver(vp::ComponentConf &config)
    : vp::Component(config), sync_done_event(this, Driver::sync_done_handler),
    check_event(this, Driver::check_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::TraceLevel::DEBUG);

    js::Config *targets_cfg = this->get_js_config()->get("targets");
    this->nb_generators = targets_cfg->get_size();
    this->targets.resize(this->nb_generators);
    for (int i = 0; i < this->nb_generators; i++)
    {
        this->targets[i] = targets_cfg->get_elem(i)->get_uint();
    }

    this->transfer_size = this->get_js_config()->get_uint("transfer_size");
    this->packet_size = this->get_js_config()->get_uint("packet_size");

    this->generator_itf.resize(this->nb_generators);
    this->generator_done.resize(this->nb_generators, false);
    this->syncs.resize(this->nb_generators);
    for (int i = 0; i < this->nb_generators; i++)
    {
        this->new_master_port("generator_" + std::to_string(i), &this->generator_itf[i]);
        this->syncs[i].event = &this->sync_done_event;
    }

    this->stats.register_stat(&this->stat_bytes_issued, "bytes_issued", "Total bytes injected into the NoC");
    this->stats.register_stat(&this->stat_bandwidth, "bandwidth", "Average injected bandwidth");
    this->stat_bandwidth.set_source(&this->stat_bytes_issued);
}

void Driver::reset(bool active)
{
    if (!active)
    {
        this->trace.msg(vp::TraceLevel::INFO, "Starting %d dummy traffic generators, each targeting its own address\n",
            this->nb_generators);

        for (int i = 0; i < this->nb_generators; i++)
        {
            this->generator_itf[i].start(this->targets[i], this->transfer_size, this->packet_size,
                &this->syncs[i], true, false);

            this->stat_bytes_issued += this->transfer_size;

            // start(...) above only registers the generator with its sync
            // object (TrafficGeneratorSync::add_generator); nothing
            // actually begins transferring until sync.start() is called.
            // Each generator gets its own sync (see driver.hpp), so this
            // starts it immediately instead of waiting on the others.
            this->syncs[i].start();
        }

        this->check_event.enqueue(10);
    }
}

void Driver::check_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Driver *_this = (Driver *)__this;

    bool all_done = true;
    for (int i = 0; i < _this->nb_generators; i++)
    {
        if (_this->generator_itf[i].is_finished())
        {
            if (!_this->generator_done[i])
            {
                _this->generator_done[i] = true;
                _this->trace.msg(vp::TraceLevel::INFO, "Generator %d finished (target: 0x%lx)\n",
                    i, _this->targets[i]);
            }
        }
        else
        {
            all_done = false;
        }
    }

    if (all_done)
    {
        _this->trace.msg(vp::TraceLevel::INFO, "All generators finished, stopping simulation\n");
        _this->time.get_engine()->quit(0);
    }
    else
    {
        _this->check_event.enqueue(10);
    }
}

void Driver::sync_done_handler(vp::Block *__this, vp::ClockEvent *event)
{
    // No-op: completion is already detected independently by check_handler
    // polling is_finished() on each generator. This handler only exists
    // because TrafficGeneratorSync requires a non-null event to enqueue
    // once every registered generator finishes.
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Driver(config);
}
