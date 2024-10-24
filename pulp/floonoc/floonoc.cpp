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
    this->width = get_js_config()->get("width")->get_int();
    this->dim_x = get_js_config()->get_int("dim_x");
    this->dim_y = get_js_config()->get_int("dim_y");
    this->router_input_queue_size = get_js_config()->get_int("router_input_queue_size");

    // Reserve the array for the target. We may have one target at each node.
    this->targets.resize(this->dim_x * this->dim_y);

    // Go through the mappings to create one master IO interface for each target
    js::Config *mappings = get_js_config()->get("mappings");
    if (mappings != NULL)
    {
        // For now entries are stored in a classic array. When a request is received, we will
        // to compare to each entry which may be slow when having lots of target.
        // We could optimize it by using a tree.
        this->entries.resize(mappings->get_childs().size());
        int id = 0;
        for (auto& mapping: mappings->get_childs())
        {
            // For each mapping we create the master interface where we'll forward request to the
            // target
            js::Config *config = mapping.second;

            vp::IoMaster *itf = new vp::IoMaster();

            itf->set_resp_meth(&FlooNoc::response);
            itf->set_grant_meth(&FlooNoc::grant);
            this->new_master_port(mapping.first, itf);

            // And we add an entry so that we can turn an address into a target position
            this->entries[id].base = config->get_uint("base");
            this->entries[id].size = config->get_uint("size");
            this->entries[id].x = config->get_int("x");
            this->entries[id].y = config->get_int("y");
            this->entries[id].remove_offset = config->get_uint("remove_offset");

            // Once a request reaches the right position, the target will be retrieved through
            // this array indexed by the position
            this->targets[this->entries[id].y * this->dim_x + this->entries[id].x] = itf;

            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Adding target (name: %s, base: 0x%x, size: 0x%x, x: %d, y: %d, remove_offset: 0x%x)\n",
                mapping.first.c_str(), this->entries[id].base, this->entries[id].size, this->entries[id].x, this->entries[id].y, this->entries[id].remove_offset);

            id++;
        }
    }

    // Create the array of networks interfaces
    this->network_interfaces.resize(this->dim_x * this->dim_y);
    js::Config *network_interfaces = get_js_config()->get("network_interfaces");
    if (network_interfaces != NULL)
    {
        for (js::Config *network_interface: network_interfaces->get_elems())
        {
            int x = network_interface->get_elem(0)->get_int();
            int y = network_interface->get_elem(1)->get_int();

            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Adding network interface (x: %d, y: %d)\n", x, y);

            this->network_interfaces[y*this->dim_x + x] = new NetworkInterface(this, x, y);
        }
    }

    // Create the array of routers
    this->routers.resize(this->dim_x * this->dim_y);
    js::Config *routers = get_js_config()->get("network_interfaces");
    if (routers != NULL)
    {
        for (js::Config *router: routers->get_elems())
        {
            int x = router->get_elem(0)->get_int();
            int y = router->get_elem(1)->get_int();

            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Adding router (x: %d, y: %d)\n", x, y);

            this->routers[y*this->dim_x + x] = new Router(this, x, y, this->router_input_queue_size);
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
    NetworkInterface *ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_DEST_NI);
    ni->handle_response(req);
}



Router *FlooNoc::get_router(int x, int y)
{
    return this->routers[y * this->dim_x + x];
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
    FlooNoc *_this = (FlooNoc *)__this;
    // Just notify the end of request to account it in the network interface
    _this->handle_request_end(req);
}



// This gets called after a request sent to a target was denied, and it is now granted
void FlooNoc::grant(vp::Block *__this, vp::IoReq *req)
{
    // When the request sent by the router to the target was denied, the router was stored in
    // the request to notify it when the request is granted.
    // Get back the router and forward the grant
    FlooNoc *_this = (FlooNoc *)__this;
    Router *router = *(Router **)req->arg_get(FlooNoc::REQ_ROUTER);
    router->grant(req);
}



void FlooNoc::reset(bool active)
{
}



Entry *FlooNoc::get_entry(uint64_t base, uint64_t size)
{
    // For now, we store mapping in a classic array.
    // Just go through each entry one by one, until one is matching the requested memory location
    for (int i=0; i<this->entries.size(); i++)
    {
        Entry *entry = &this->entries[i];
        if (base >= entry->base && base + size <= entry->base + entry->size)
        {
            return entry;
        }
    }
    return NULL;
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new FlooNoc(config);
}
