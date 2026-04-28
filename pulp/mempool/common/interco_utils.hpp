/*
 * Copyright (C) 2025 ETH Zurich and University of Bologna
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

#pragma once

#include <algorithm>
#include <cstdint>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

#ifndef GET_BITS
#define GET_BITS(x, start, end) \
    (((x) >> (start)) & ((1U << ((end) - (start) + 1)) - 1))
#endif

#ifndef SET_BITS
#define SET_BITS(x, val, start, end) \
    ((x) = ((x) & ~(((1U << ((end) - (start) + 1)) - 1) << (start))) | \
           (((val) & ((1U << ((end) - (start) + 1)) - 1)) << (start)))
#endif

inline unsigned int clog2(int value)
{
    unsigned int result = 0;
    value--;
    while (value > 0)
    {
        value >>= 1;
        result++;
    }
    return result;
}

/**
 * @brief Bandwidth limiter shared by mempool/teranoc interconnect components.
 *
 * Apply per-request bandwidth and latency accounting. Pass a trace pointer to
 * emit the legacy update-cyclestamp message; pass nullptr to stay quiet.
 * Throttle is opt-in (default 0 disables it) so call sites that didn't have
 * throttling before keep their previous behavior.
 */
class BandwidthLimiter
{
public:
    BandwidthLimiter(int64_t bandwidth, int64_t latency,
                     bool shared_rw_bandwidth = false,
                     int throttle = 0,
                     vp::Trace *trace = nullptr)
        : bandwidth(bandwidth), latency(latency),
          shared_rw_bandwidth(shared_rw_bandwidth),
          throttle(throttle), trace(trace)
    {
    }

    void apply_bandwidth(int64_t cycles, vp::IoReq *req)
    {
        uint64_t size = req->get_size();

        if (this->bandwidth != 0)
        {
            int64_t burst_duration = (size + this->bandwidth - 1) / this->bandwidth;
            if (this->throttle > 0)
            {
                burst_duration *= (this->throttle + 1);
            }

            req->set_duration(burst_duration);

            int64_t *next_burst_cycle = (req->get_is_write() || this->shared_rw_bandwidth)
                ? &this->next_write_burst_cycle
                : &this->next_read_burst_cycle;
            int64_t router_latency = *next_burst_cycle - cycles;
            int64_t req_latency = std::max((int64_t)req->get_latency(), router_latency);

            req->set_latency(req_latency + this->latency);

            *next_burst_cycle = std::max(cycles, *next_burst_cycle) + burst_duration;

            if (this->trace)
            {
                this->trace->msg(vp::Trace::LEVEL_TRACE,
                    "Updating %s burst bandwidth cyclestamp (bandwidth: %d, next_burst: %d)\n",
                    req->get_is_write() ? "write" : "read",
                    this->bandwidth, *next_burst_cycle);
            }
        }
        else
        {
            req->inc_latency(this->latency);
        }
    }

private:
    int64_t bandwidth;
    int64_t latency;
    bool shared_rw_bandwidth;
    int throttle;
    vp::Trace *trace;
    int64_t next_read_burst_cycle = 0;
    int64_t next_write_burst_cycle = 0;
};
