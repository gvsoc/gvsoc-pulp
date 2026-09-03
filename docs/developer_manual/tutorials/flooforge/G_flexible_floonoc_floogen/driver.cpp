#include <vp/vp.hpp>
#include "driver.hpp"

Driver::Driver(vp::ComponentConf &config)
    : vp::Component(config), check_event(this, Driver::check_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::TraceLevel::DEBUG);

    this->nb_generators = this->get_js_config()->get_int("nb_generators");
    this->target_base = this->get_js_config()->get_uint("target_base");
    this->transfer_size = this->get_js_config()->get_uint("transfer_size");
    this->packet_size = this->get_js_config()->get_uint("packet_size");

    this->generator_itf.resize(this->nb_generators);
    for (int i = 0; i < this->nb_generators; i++)
    {
        this->new_master_port("generator_" + std::to_string(i), &this->generator_itf[i]);
    }
}

void Driver::reset(bool active)
{
    if (!active)
    {
        this->trace.msg(vp::TraceLevel::INFO, "Starting %d dummy traffic generators towards 0x%lx\n",
            this->nb_generators, this->target_base);

        for (int i = 0; i < this->nb_generators; i++)
        {
            this->generator_itf[i].start(this->target_base, this->transfer_size, this->packet_size,
                &this->sync, true, false);
        }

        this->check_event.enqueue(100);
    }
}

void Driver::check_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Driver *_this = (Driver *)__this;

    bool all_done = true;
    for (int i = 0; i < _this->nb_generators; i++)
    {
        if (!_this->generator_itf[i].is_finished())
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
        _this->check_event.enqueue(100);
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Driver(config);
}
