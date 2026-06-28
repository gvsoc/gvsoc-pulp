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
    void process_req(vp::IoReq *req);

    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);

    // This component trace
    vp::Trace trace;

    std::queue<vp::IoReq *> denied_reqs;
    int64_t next_req_cycle = 0;
    int64_t next_proc_cycle = 0;
    bool stalled;
    int req_latency;

    vp::ClockEvent fsm_event;
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    BandwidthLimiter *resp_bw_limiter;
    int resp_latency;
    vp::Queue forward_queue;
    vp::Queue backward_queue;

    vp::IoSlave input_itf;
    vp::IoMaster output_itf;
};



L1_RemoteItf::L1_RemoteItf(vp::ComponentConf &config)
    : vp::Component(config), fsm_event(this, L1_RemoteItf::fsm_handler),
    forward_queue(this, "forward_queue"), backward_queue(this, "backward_queue")
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    stalled = false;
    req_latency = this->get_js_config()->get_int("req_latency");
    resp_latency = this->get_js_config()->get_int("resp_latency");
    int bandwidth = this->get_js_config()->get_int("bandwidth");
    bool shared_rw_bandwidth = this->get_js_config()->get_child_bool("shared_rw_bandwidth");

    this->resp_bw_limiter = new BandwidthLimiter(bandwidth, resp_latency, shared_rw_bandwidth, 0, &this->trace);

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
    vp_assert(req->get_latency() == 0, this->get_trace(), "L1_RemoteItf: req latency should be 0 when received\n");

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n",
        offset, size, is_write);

    // First apply the bandwidth limitation of request input
    int64_t cycles = this->clock.get_cycles();
    if (this->stalled || cycles < this->next_req_cycle || this->denied_reqs.size() > 0)
    {
        this->denied_reqs.push(req);
        this->fsm_event.enqueue();
        return vp::IO_REQ_DENIED;
    }

    this->next_req_cycle = cycles + 1;
    if (this->req_latency > 0 || this->next_proc_cycle > cycles || this->forward_queue.size() > 0)
    {
        this->forward_queue.push_back(req, (int64_t)this->req_latency - 1);
        this->fsm_event.enqueue();
        return vp::IO_REQ_PENDING;
    }

    this->process_req(req);

    return vp::IO_REQ_PENDING;
}

void L1_RemoteItf::process_req(vp::IoReq *req)
{
    int64_t cycles = this->clock.get_cycles();
    vp::IoSlave *resp_port = req->resp_port;
    req->arg_push((void *)resp_port);
    this->next_proc_cycle = cycles + 1;
    vp::IoReqStatus retval = this->output_itf.req(req);

    if (retval == vp::IO_REQ_OK)
    {
        req->arg_pop();
        req->resp_port = resp_port;
        this->resp_bw_limiter->apply_bandwidth(cycles, req);
        int latency = req->get_latency();
        this->backward_queue.push_delayed(req, latency);
        if (latency > (this->resp_latency + 1))
        {
            this->next_req_cycle += (latency - (this->resp_latency + 1));
            this->next_proc_cycle += (latency - (this->resp_latency + 1));
        }
        this->fsm_event.enqueue();
    }
    else if(retval == vp::IO_REQ_DENIED)
    {
        this->stalled = true;
    }
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
    _this->backward_queue.push_delayed(req, latency);
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
        _this->next_req_cycle = cycles + 1;
        _this->forward_queue.push_back(req, (int64_t)_this->req_latency - 1);
    }

    if (!_this->stalled && cycles >= _this->next_proc_cycle && !_this->forward_queue.empty())
    {
        vp::IoReq *req = (vp::IoReq *)_this->forward_queue.head();
        _this->forward_queue.pop();
        _this->process_req(req);
    }

    if (!_this->backward_queue.empty())
    {
        vp::IoReq *req = (vp::IoReq *)_this->backward_queue.pop();
        req->resp_port->resp(req);
    }

    if (!_this->stalled && (_this->denied_reqs.size() > 0 || _this->forward_queue.size() > 0))
    {
        _this->fsm_event.enqueue();
    }

    if (_this->backward_queue.size() > 0)
    {
        _this->fsm_event.enqueue();
    }

}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new L1_RemoteItf(config);
}
