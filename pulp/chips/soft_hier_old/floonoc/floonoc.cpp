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
    this->atomics = get_js_config()->get_int("atomics");
    this->collective = get_js_config()->get_int("collective");
    this->edge_node_alias = get_js_config()->get_int("edge_node_alias");
    this->edge_node_alias_start_bit = get_js_config()->get_int("edge_node_alias_start_bit");
    this->interleave_enable = get_js_config()->get_int("interleave_enable");
    this->interleave_region_base = get_js_config()->get_int("interleave_region_base");
    this->interleave_region_size = get_js_config()->get_int("interleave_region_size");
    this->interleave_granularity = get_js_config()->get_int("interleave_granularity");
    this->interleave_bit_start = get_js_config()->get_int("interleave_bit_start");
    this->interleave_bit_width = get_js_config()->get_int("interleave_bit_width");

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

            // Once a request reaches the right position, the target will be retrieved through
            // this array indexed by the position
            this->targets[this->entries[id].y * this->dim_x + this->entries[id].x] = itf;

            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Adding target (name: %s, base: 0x%llx, size: 0x%llx, x: %d, y: %d)\n",
                mapping.first.c_str(), this->entries[id].base, this->entries[id].size, this->entries[id].x, this->entries[id].y);

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
void process_collective_operations(vp::IoReq *parent, vp::IoReq *req);
void FlooNoc::handle_request_end(vp::IoReq *req)
{
    if (*req->arg_get(FlooNoc::REQ_PARENT) != NULL)
    {
        vp::IoReq * parent = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_PARENT);
        process_collective_operations(parent, req);
        delete req->get_data();
        delete req;
        parent->set_int(FlooNoc::REQ_PEND_KIDS, parent->get_int(FlooNoc::REQ_PEND_KIDS) - 1);
        if (parent->get_int(FlooNoc::REQ_PEND_KIDS) <= 0)
        {
            this->handle_request_end(parent);
        }
    } else
    {
        NetworkInterface *ni = *(NetworkInterface **)req->arg_get(FlooNoc::REQ_DEST_NI);
        ni->handle_response(req);
    }
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


/****************************************************
*                   FP16 Utilities                  *
****************************************************/

typedef union {
    float f;
    struct {
        uint32_t mantissa : 23;
        uint32_t exponent : 8;
        uint32_t sign : 1;
    } parts;
} FloatBits;

typedef uint16_t fp16;

// Convert float to FP16 (half-precision)
fp16 float_to_fp16(float value) {
    FloatBits floatBits;
    floatBits.f = value;

    uint16_t sign = floatBits.parts.sign << 15;
    int32_t exponent = floatBits.parts.exponent - 127 + 15; // adjust bias from 127 to 15
    uint32_t mantissa = floatBits.parts.mantissa >> 13;     // reduce to 10 bits

    if (exponent <= 0) {
        if (exponent < -10) return sign;   // too small
        mantissa = (floatBits.parts.mantissa | 0x800000) >> (1 - exponent);
        return sign | mantissa;
    } else if (exponent >= 0x1F) {
        return sign | 0x7C00;  // overflow to infinity
    }
    return sign | (exponent << 10) | mantissa;
}

// Convert FP16 to float
float fp16_to_float(fp16 value) {
    FloatBits floatBits;
    floatBits.parts.sign = (value >> 15) & 0x1;
    int32_t exponent = (value >> 10) & 0x1F;
    floatBits.parts.exponent = (exponent == 0) ? 0 : exponent + 127 - 15;
    floatBits.parts.mantissa = (value & 0x3FF) << 13;
    return floatBits.f;
}

void process_collective_operations(vp::IoReq *parent, vp::IoReq *req)
{
    int collective_type = parent->get_int(FlooNoc::REQ_COLL_TYPE);

    if (collective_type == 1)
    {
        // this->trace.msg(vp::Trace::LEVEL_DEBUG, "[BroadCast]\n");

    } else if (collective_type == 2){
        // this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction ADD UINT16]\n");
        //Execute reduction
        uint16_t * dst = (uint16_t *) parent->get_data();
        uint16_t * src = (uint16_t *) req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(uint16_t)); ++i)
        {
            dst[i] = dst[i] + src[i];
        }
    } else if (collective_type == 3){
        // this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction ADD INT16]\n");
        //Execute reduction
        int16_t * dst = (int16_t *) parent->get_data();
        int16_t * src = (int16_t *) req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(int16_t)); ++i)
        {
            dst[i] = dst[i] + src[i];
        }
    } else if (collective_type == 4){
        // this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction ADD FP16]\n");
        //Execute reduction
        fp16 * dst = (fp16 *) parent->get_data();
        fp16 * src = (fp16 *) req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(fp16)); ++i)
        {
            dst[i] = float_to_fp16(fp16_to_float(dst[i]) + fp16_to_float(src[i]));
        }
    } else if (collective_type == 5){
        // this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction MAX UINT16]\n");
        //Execute reduction
        uint16_t * dst = (uint16_t *) parent->get_data();
        uint16_t * src = (uint16_t *) req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(uint16_t)); ++i)
        {
            dst[i] = dst[i] > src[i] ? dst[i] : src[i];
        }
    } else if (collective_type == 6){
        // this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction MAX INT16]\n");
        //Execute reduction
        int16_t * dst = (int16_t *) parent->get_data();
        int16_t * src = (int16_t *) req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(int16_t)); ++i)
        {
            dst[i] = dst[i] > src[i] ? dst[i] : src[i];
        }
    } else if (collective_type == 7){
        // this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Reduction MAX FP16]\n");
        //Execute reduction
        fp16 * dst = (fp16 *) parent->get_data();
        fp16 * src = (fp16 *) req->get_data();
        for (int i = 0; i < (req->get_size()/sizeof(fp16)); ++i)
        {
            dst[i] = float_to_fp16(fp16_to_float(dst[i]) > fp16_to_float(src[i]) ? fp16_to_float(dst[i]) : fp16_to_float(src[i]));
        }
    } else {
        // this->trace.fatal("Invalid collective operation: %d\n", collective_type);
    }
}
