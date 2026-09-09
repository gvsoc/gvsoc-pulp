#pragma once

#include <vector>
#include <vp/vp.hpp>
#include <vp/stats/stats.hpp>
#include "interco/traffic/generator.hpp"

class Driver : public vp::Component
{
public:
    Driver(vp::ComponentConf &config);

private:
    void reset(bool active) override;
    static void check_handler(vp::Block *__this, vp::ClockEvent *event);
    static void sync_done_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::Trace trace;
    int nb_generators;
    std::vector<uint64_t> targets;
    uint64_t transfer_size;
    uint64_t packet_size;
    std::vector<TrafficGeneratorConfigMaster> generator_itf;
    std::vector<bool> generator_done;
    vp::ClockEvent sync_done_event;
    std::vector<TrafficGeneratorSync> syncs;
    vp::ClockEvent check_event;

    vp::StatScalar stat_bytes_issued;
    vp::StatBw stat_bandwidth;
};
