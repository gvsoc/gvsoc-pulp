/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 *          Jonas Martin, ETH (martinjo@student.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "floonoc.hpp"
#include "floonoc_router.hpp"
#include "floonoc_network_interface.hpp"


FlooNoc::FlooNoc(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
    // Get properties from generator
    this->narrow_width = get_js_config()->get("narrow_width")->get_int();
    this->dim_x = get_js_config()->get_int("dim_x");
    this->dim_y = get_js_config()->get_int("dim_y");
    this->router_input_queue_size = get_js_config()->get_int("router_input_queue_size");
    this->router_output_queue_size = get_js_config()->get_int("router_output_queue_size");

    // Reserve the array for the target. We may have one target at each node.
    this->targets.resize(this->dim_x * this->dim_y);
    for (int x = 0; x < this->dim_x; x++)
    {
        for (int y = 0; y < this->dim_y; y++)
        {
            vp::IoMaster *itf = new vp::IoMaster();
            this->targets[y * this->dim_x + x] = itf;
            itf->set_resp_meth(&FlooNoc::response);
            itf->set_grant_meth(&FlooNoc::grant);
            this->new_master_port("out_" + std::to_string(x) + "_" + std::to_string(y), itf);
        }
    }

    // Create the array of networks interfaces
    this->network_interfaces.resize(this->dim_x * this->dim_y);
    for (int x = 0; x < this->dim_x; x++)
    {
        for (int y = 0; y < this->dim_y; y++)
        {
            this->network_interfaces[y*this->dim_x + x] = new NetworkInterface(this, x, y);
        }
    }

    // Create the array of routers
    this->req_routers.resize(this->dim_x * this->dim_y);
    for (int x = 0; x < this->dim_x; x++)
    {
        for (int y = 0; y < this->dim_y; y++)
        {
            this->req_routers[y*this->dim_x + x] = new Router(this,"req_router_", x, y, this->router_input_queue_size);
        }
    }
}


// This notifies the end of an internal requests, which is part of an external burst
// This gets called in 2 different ways:
// - When a router gets a synchronous response
// - When an interface receives a call to the response callback
// In both cases, the requests is accounted on the initiator burst, in the network interface
void FlooNoc::handle_request_end(vp::IoReq *req)
{
    // NetworkInterface *ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_SRC_NI);
    // ni->handle_response(req);
}



Router *FlooNoc::get_req_router(int x, int y)
{
    return this->req_routers[y * this->dim_x + x];
}



NetworkInterface *FlooNoc::get_network_interface(int x, int y)
{
    return this->network_interfaces[y * this->dim_x + x];
}



vp::IoMaster *FlooNoc::get_target(int x, int y)
{
    return this->targets[y * this->dim_x + x];
}



// This gets called when a request was pending and the response is received
void FlooNoc::response(vp::Block *__this, vp::IoReq *req)
{
    // FlooNoc *_this = (FlooNoc *)__this;
    // // Just notify the end of request to account it in the network interface
    // _this->handle_request_end(req);
}



// This gets called after a request sent to a target was denied, and it is now granted
void FlooNoc::grant(vp::Block *__this, vp::IoReq *req)
{
    // When the request sent by the NI to the target was denied, the NI was stored in
    // the request to notify it when the request is granted.
    // Get back the NI and forward the grant
    FlooNoc *_this = (FlooNoc *)__this;
    NetworkInterface *ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_NI);
    ni->grant(req);
}



void FlooNoc::reset(bool active)
{
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new FlooNoc(config);
}
