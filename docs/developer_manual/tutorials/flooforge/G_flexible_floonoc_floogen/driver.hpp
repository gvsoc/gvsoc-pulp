#pragma once

#include <vector>
#include <vp/vp.hpp>
#include "interco/traffic/generator.hpp"

class Driver : public vp::Component
{
public:
    Driver(vp::ComponentConf &config);

private:
    void reset(bool active) override;
    static void check_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::Trace trace;
    int nb_generators;
    uint64_t target_base;
    uint64_t transfer_size;
    uint64_t packet_size;
    std::vector<TrafficGeneratorConfigMaster> generator_itf;
    TrafficGeneratorSync sync;
    vp::ClockEvent check_event;
};
