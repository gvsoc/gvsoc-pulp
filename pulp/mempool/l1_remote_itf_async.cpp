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

    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);

    // This component trace
    vp::Trace trace;

    std::queue<vp::IoReq *> denied_reqs;
    int64_t next_req_cycle = 0;
    bool stalled;
    int req_latency;

    vp::ClockEvent fsm_event;
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    BandwidthLimiter *resp_bw_limiter;
    int resp_latency;
    vp::Queue out_queue;

    vp::IoSlave input_itf;
    vp::IoMaster output_itf;
};



L1_RemoteItf::L1_RemoteItf(vp::ComponentConf &config)
    : vp::Component(config), fsm_event(this, L1_RemoteItf::fsm_handler), out_queue(this, "out_queue")
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    stalled = false;
    req_latency = this->get_js_config()->get_int("req_latency");
    resp_latency = this->get_js_config()->get_int("resp_latency");
    int bandwidth = this->get_js_config()->get_int("bandwidth");
    bool shared_rw_bandwidth = this->get_js_config()->get_child_bool("shared_rw_bandwidth");

    this->resp_bw_limiter = new BandwidthLimiter(this, bandwidth, resp_latency, shared_rw_bandwidth);

    this->input_itf.set_req_meth(&L1_RemoteItf::req);
    this->output_itf.set_grant_meth(&L1_RemoteItf::grant);
    this->output_itf.set_resp_meth(&L1_RemoteItf::response);
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
    int64_t cycles = this->clock.get_cycles();
    if (cycles < this->next_req_cycle || this->denied_reqs.size() > 0)
    {
        this->denied_reqs.push(req);
        this->fsm_event.enqueue();
        return vp::IO_REQ_DENIED;
    }

    req->inc_latency(this->req_latency);
    this->next_req_cycle = cycles + 1;

    // The mapping may exist and not be connected, return an error in this case
    if (!this->output_itf.is_bound())
    {
        this->trace.fatal("L1_RemoteItf: output port is not connected\n");
        return vp::IO_REQ_INVALID;
    }

    vp::IoSlave *resp_port = req->resp_port;
    req->arg_push((void *)resp_port);
    vp::IoReqStatus retval = this->output_itf.req(req);

    if (retval == vp::IO_REQ_OK)
    {
        req->arg_pop();
        req->resp_port = resp_port;
        this->resp_bw_limiter->apply_bandwidth(cycles, req);
        int latency = req->get_latency();
        this->out_queue.push_back(req, latency > 0 ? latency - 1 : 0);
        if (latency > (this->req_latency + this->resp_latency + 1))
        {
            this->next_req_cycle += (latency - (this->req_latency + this->resp_latency + 1));
        }
        this->fsm_event.enqueue();
    }
    else if(retval == vp::IO_REQ_DENIED)
    {
        this->stalled = true;
    }

    return vp::IO_REQ_PENDING;
}

void L1_RemoteItf::grant(vp::Block *__this, vp::IoReq *req)
{
    L1_RemoteItf *_this = (L1_RemoteItf *)__this;
    _this->stalled = false;
    _this->fsm_event.enqueue();
}

void L1_RemoteItf::response(vp::Block *__this, vp::IoReq *req)
{
    L1_RemoteItf *_this = (L1_RemoteItf *)__this;
    int64_t cycles = _this->clock.get_cycles();
    vp::IoSlave *resp_port = (vp::IoSlave *)req->arg_pop();
    req->resp_port = resp_port;
    req->prepare();
    _this->resp_bw_limiter->apply_bandwidth(cycles, req);
    int latency = req->get_latency();
    _this->out_queue.push_back(req, latency > 0 ? latency - 1 : 0);
    _this->fsm_event.enqueue();
}

void L1_RemoteItf::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    L1_RemoteItf *_this = (L1_RemoteItf *)__this;
    int64_t cycles = _this->clock.get_cycles();

    if (!_this->stalled && cycles >= _this->next_req_cycle && _this->denied_reqs.size() > 0)
    {
        vp::IoReq *req = _this->denied_reqs.front();
        _this->denied_reqs.pop();
        req->resp_port->grant(req);

        req->inc_latency(_this->req_latency);
        _this->next_req_cycle = cycles + 1;

        // The mapping may exist and not be connected, return an error in this case
        if (!_this->output_itf.is_bound())
        {
            _this->trace.fatal("L1_RemoteItf: output port is not connected\n");
        }

        vp::IoSlave *resp_port = req->resp_port;
        req->arg_push((void *)resp_port);
        vp::IoReqStatus retval = _this->output_itf.req(req);

        if (retval == vp::IO_REQ_OK)
        {
            req->arg_pop();
            req->resp_port = resp_port;
            _this->resp_bw_limiter->apply_bandwidth(cycles, req);
            int latency = req->get_latency();
            _this->out_queue.push_back(req, latency > 0 ? latency - 1 : 0);
            if (latency > (_this->req_latency + _this->resp_latency + 1))
            {
                _this->next_req_cycle += (latency - (_this->req_latency + _this->resp_latency + 1));
            }
            _this->fsm_event.enqueue();
        }
        else if(retval == vp::IO_REQ_DENIED)
        {
            _this->stalled = true;
        }
    }
    if (!_this->stalled && _this->denied_reqs.size() > 0)
    {
        _this->fsm_event.enqueue();
    }

    if (!_this->out_queue.empty())
    {
        vp::IoReq *req = (vp::IoReq *)_this->out_queue.pop();
        req->resp_port->resp(req);
    }

    if (_this->out_queue.size() > 0)
    {
        _this->fsm_event.enqueue();
    }

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
