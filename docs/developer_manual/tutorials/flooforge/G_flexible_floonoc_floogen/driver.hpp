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
    // TrafficGeneratorSync is a barrier across every generator registered on
    // it: none of them advance past a phase (pre-check / transfer /
    // post-check / end) until *all* generators sharing that sync object
    // have reached it. Each generator here targets a different tile at a
    // different mesh distance, so they must NOT share one sync object -
    // doing so would lock-step them all to the slowest one. Every generator
    // gets its own. It still unconditionally calls event->enqueue() once
    // done, so each needs a real event even though we don't act on it -
    // completion is already detected independently by check_handler polling
    // is_finished() on each generator.
    vp::ClockEvent sync_done_event;
    std::vector<TrafficGeneratorSync> syncs;
    vp::ClockEvent check_event;

    vp::StatScalar stat_bytes_issued;
    vp::StatBw stat_bandwidth;
};
