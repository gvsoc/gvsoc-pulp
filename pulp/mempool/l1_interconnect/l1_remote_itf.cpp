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
#include "pulp/mempool/common/interco_utils.hpp"

class L1_RemoteItf : public vp::Component
{
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
    int throttle;

    vp::IoSlave input_itf;
    vp::IoMaster output_itf;
};



L1_RemoteItf::L1_RemoteItf(vp::ComponentConf &config)
    : vp::Component(config)
{
    traces.new_trace("trace", &trace, vp::DEBUG);
    req_latency = this->get_js_config()->get_int("req_latency");
    resp_latency = this->get_js_config()->get_int("resp_latency");
    int bandwidth = this->get_js_config()->get_int("bandwidth");
    bool shared_rw_bandwidth = this->get_js_config()->get_child_bool("shared_rw_bandwidth");
    throttle = this->get_js_config()->get_int("throttle");

    this->req_bw_limiter = new BandwidthLimiter(bandwidth, req_latency, shared_rw_bandwidth, throttle, &this->trace);
    this->resp_bw_limiter = new BandwidthLimiter(bandwidth, resp_latency, shared_rw_bandwidth, 0, &this->trace);

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

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new L1_RemoteItf(config);
}
