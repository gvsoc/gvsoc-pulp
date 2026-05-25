/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include "floonoc.hpp"

class L1NocRouterRemapper : public vp::Component
{

public:
    L1NocRouterRemapper(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req, int port);
    static void grant(vp::Block *__this, vp::IoReq *req, int id);
    static void response(vp::Block *__this, vp::IoReq *req, int id);

    vp::Trace trace;
    std::vector<vp::IoSlave> input_itfs;
    std::vector<vp::IoMaster> output_itfs;
    std::vector<vp::IoReq *> pending_reqs;
    std::vector<vp::IoSlave *> original_itfs;

    vp::ClockEvent fsm_event;
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    int get_mapped_port(int port);

    int nb_ports;
    int remap_batch_size;
    int nb_groups;
    bool shuffle;
    int remap_pos;
    uint64_t remap_timestamp;
};



L1NocRouterRemapper::L1NocRouterRemapper(vp::ComponentConf &config)
    : vp::Component(config), fsm_event(this, L1NocRouterRemapper::fsm_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->nb_ports = get_js_config()->get_int("nb_ports");
    this->remap_batch_size = get_js_config()->get_int("remap_batch_size");
    this->nb_groups = this->nb_ports / this->remap_batch_size;
    this->shuffle = get_js_config()->get_child_bool("shuffle");
    this->remap_pos = 0;
    this->remap_timestamp = 0;

    this->input_itfs.resize(this->nb_ports);
    for (int i = 0; i < this->nb_ports; i++)
    {
        this->input_itfs[i].set_req_meth_muxed(&L1NocRouterRemapper::req, i);
        this->new_slave_port("input_" + std::to_string(i), &this->input_itfs[i]);
    }
    this->output_itfs.resize(this->nb_ports);
    for (int i = 0; i < this->nb_ports; i++)
    {
        this->output_itfs[i].set_resp_meth_muxed(&L1NocRouterRemapper::response, i);
        this->output_itfs[i].set_grant_meth_muxed(&L1NocRouterRemapper::grant, i);
        this->new_master_port("output_" + std::to_string(i), &this->output_itfs[i]);
    }
    this->pending_reqs.resize(this->nb_ports, nullptr);
    this->original_itfs.resize(this->nb_ports, nullptr);
}

vp::IoReqStatus L1NocRouterRemapper::req(vp::Block *__this, vp::IoReq *req, int port)
{
    L1NocRouterRemapper *_this = (L1NocRouterRemapper *)__this;
    if (_this->pending_reqs[port] != nullptr) {
        _this->trace.fatal("No request should be sent while another is pending on the same port\n");
        return vp::IO_REQ_INVALID;
    }
    int mapped_port = _this->get_mapped_port(port);
    *req->arg_get_last() = (void *)1;
    _this->original_itfs[port] = req->resp_port;
    vp::IoReqStatus retval = _this->output_itfs[mapped_port].req(req);
    if (retval == vp::IO_REQ_PENDING) {
        return vp::IO_REQ_PENDING;
    }
    else if(retval == vp::IO_REQ_DENIED) {
        _this->pending_reqs[port] = req;
        _this->fsm_event.enqueue();
        return vp::IO_REQ_DENIED;
    }
    else {
        _this->trace.fatal("Unexpected return value from output interface req\n");
        return vp::IO_REQ_INVALID;
    }
}

void L1NocRouterRemapper::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    L1NocRouterRemapper *_this = (L1NocRouterRemapper *)__this;
    for (int i = 0; i < _this->nb_ports; i++) {
        if (_this->pending_reqs[i] != nullptr) {
            vp::IoReq *req = _this->pending_reqs[i];
            int mapped_port = _this->get_mapped_port(i);
            vp::IoReqStatus retval = _this->output_itfs[mapped_port].req(req);
            if (retval == vp::IO_REQ_PENDING) {
                _this->original_itfs[i]->grant(req);
                _this->pending_reqs[i] = nullptr;
            }
            else if(retval == vp::IO_REQ_DENIED) {
                // still pending
                _this->fsm_event.enqueue();
            }
            else {
                _this->trace.fatal("Unexpected return value from output interface req in fsm\n");
            }
        }
    }
}

void L1NocRouterRemapper::grant(vp::Block *__this, vp::IoReq *req, int id)
{
    L1NocRouterRemapper *_this = (L1NocRouterRemapper *)__this;
    _this->trace.fatal("L1NocRouterRemapper::grant should not be called\n");
}

void L1NocRouterRemapper::response(vp::Block *__this, vp::IoReq *req, int id)
{
    L1NocRouterRemapper *_this = (L1NocRouterRemapper *)__this;
    _this->trace.fatal("L1NocRouterRemapper::response should not be called\n");
}

int L1NocRouterRemapper::get_mapped_port(int port)
{
    if (this->remap_batch_size <= 1 || this->nb_ports < this->remap_batch_size) {
        return port;
    }
    this->remap_pos = (this->remap_pos + this->clock.get_cycles() - this->remap_timestamp) % this->remap_batch_size;
    this->remap_timestamp = this->clock.get_cycles();
    int offset_in_group, group_start;
    if (this->shuffle) {
        offset_in_group = port / this->nb_groups;
        group_start = (port % this->nb_groups) * this->remap_batch_size;
    }
    else {
        offset_in_group = port % this->remap_batch_size;
        group_start = port - offset_in_group;
    }
    int adjusted_offset = (offset_in_group + this->remap_pos) % this->remap_batch_size;
    int mapped_offset = (adjusted_offset % (this->remap_batch_size / 2)) * 2 + adjusted_offset / (this->remap_batch_size / 2);
    int mapped_port = group_start + mapped_offset;
    if (mapped_port < 0 || mapped_port >= this->nb_ports) {
        this->trace.fatal("Mapped port %d is out of range\n", mapped_port);
    }
    return mapped_port;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new L1NocRouterRemapper(config);
}
