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

/*
 * Authors: Yinrong Li, ETH Zurich (yinrli@student.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/signal.hpp>
#include <vp/itf/io.hpp>
#include <stdio.h>
#include <math.h>
#include <vp/mapping_tree.hpp>

class L1_RemoteItf;


/**
 * @brief Bandwidth limiter
 *
 * This is used on both input and output ports to impact requests latency and durations so that
 * the specified bandwidth is respected in average.
 *
 */
class BandwidthLimiter
{
public:
    // Overall bandwidth to be respected, and global latency to be applied to each request
    BandwidthLimiter(L1_RemoteItf *top, int64_t bandwidth, int64_t latency, bool shared_rw_bandwidth);
    // Can be called on any request going through the limiter to add the fixed latency and impact
    // the latency and duration with the current utilization of the limiter with respect to the
    // bandwidth
    void apply_bandwidth(int64_t cycles, vp::IoReq *req);

private:
    L1_RemoteItf *top;
    // Bandwidth in bytes per cycle to be respected
    int64_t bandwidth;
    // Fixed latency to be added to each request
    int64_t latency;
    // Cyclestamp at which the next read burst can go through the limiter. Used to delay a request
    // which arrives before this cyclestamp.
    int64_t next_read_burst_cycle = 0;
    // Cyclestamp at which the write read burst can go through the limiter. Used to delay a request
    // which arrives before this cyclestamp.
    int64_t next_write_burst_cycle = 0;
    // Indicates whether the read and write share the bandwidth
    bool shared_rw_bandwidth = false;
};

class L1_RemoteItf : public vp::Component
{
    friend class BandwidthLimiter;

public:
    L1_RemoteItf(vp::ComponentConf &conf);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    vp::IoReqStatus handle_req(vp::IoReq *req, int port);

    // This component trace
    vp::Trace trace;

    BandwidthLimiter *req_bw_limiter;
    BandwidthLimiter *resp_bw_limiter;
    int req_latency;
    int resp_latency;

    vp::IoSlave input_itf;
    vp::IoMaster output_itf;
};



L1_RemoteItf::L1_RemoteItf(vp::ComponentConf &config)
    : vp::Component(config)
{
    req_latency = this->get_js_config()->get_int("req_latency");
    resp_latency = this->get_js_config()->get_int("resp_latency");
    int bandwidth = this->get_js_config()->get_int("bandwidth");
    bool shared_rw_bandwidth = this->get_js_config()->get_child_bool("shared_rw_bandwidth");

    this->req_bw_limiter = new BandwidthLimiter(this, bandwidth, req_latency, shared_rw_bandwidth);
    this->resp_bw_limiter = new BandwidthLimiter(this, bandwidth, resp_latency, shared_rw_bandwidth);

    this->input_itf.set_req_meth(&L1_RemoteItf::req);
    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("output", &this->output_itf);

}

vp::IoReqStatus L1_RemoteItf::req(vp::Block *__this, vp::IoReq *req)
{
    L1_RemoteItf *_this = (L1_RemoteItf *)__this;
    return _this->handle_req(req, 0);
}

vp::IoReqStatus L1_RemoteItf::handle_req(vp::IoReq *req, int port)
{
    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();
    bool is_write = req->get_is_write();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n",
        offset, size, is_write);

    // First apply the bandwidth limitation of request input
    this->req_bw_limiter->apply_bandwidth(this->clock.get_cycles(), req);

    // The mapping may exist and not be connected, return an error in this case
    if (!this->output_itf.is_bound())
    {
        this->trace.fatal("L1_RemoteItf: output port is not connected\n");
        return vp::IO_REQ_INVALID;
    }

    // Foward the request to the output port
    vp::IoReqStatus retval = this->output_itf.req_forward(req);

    // Then apply the bandwidth limitation of response output
    this->resp_bw_limiter->apply_bandwidth(this->clock.get_cycles(), req);

    return retval;
}

BandwidthLimiter::BandwidthLimiter(L1_RemoteItf *top, int64_t bandwidth, int64_t latency, bool shared_rw_bandwidth)
{
    this->top = top;
    this->latency = latency;
    this->bandwidth = bandwidth;
    this->shared_rw_bandwidth = shared_rw_bandwidth;
}

void BandwidthLimiter::apply_bandwidth(int64_t cycles, vp::IoReq *req)
{
    uint64_t size = req->get_size();

    if (this->bandwidth != 0)
    {
        // Bandwidth was specified

        // Duration in cycles of this burst in this router according to router bandwidth
        int64_t burst_duration = (size + this->bandwidth - 1) / this->bandwidth;

        // Update burst duration
        // This will update it only if it is bigger than the current duration, in case there is a
        // slower router on the path
        req->set_duration(burst_duration);

        // Now we need to compute the start cycle of the burst, which is its latency.
        // First get the cyclestamp where the router becomes available, due to previous requests
        int64_t *next_burst_cycle = (req->get_is_write() || this->shared_rw_bandwidth) ?
            &this->next_write_burst_cycle : &this->next_read_burst_cycle;
        int64_t router_latency = *next_burst_cycle - cycles;

        // Then compare that to the request latency and take the highest to properly delay the
        // request in case the bandwidth is reached.
        int64_t latency = std::max((int64_t)req->get_latency(), router_latency);

        // Apply the computed latency and add the fixed one
        req->set_latency(latency + this->latency);

        // Update the bandwidth information by appending the new burst right after the previous one.
        *next_burst_cycle = std::max(cycles, *next_burst_cycle) + burst_duration;

        this->top->trace.msg(vp::Trace::LEVEL_TRACE, "Updating %s burst bandwidth cyclestamp (bandwidth: %d, next_burst: %d)\n",
            req->get_is_write() ? "write" : "read", this->bandwidth, *next_burst_cycle);
    }
    else
    {
        // No bandwidth was specified, just add the specified latency
        req->inc_latency(this->latency);
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new L1_RemoteItf(config);
}
