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
    this->wide_width = get_js_config()->get("wide_width")->get_int();
    this->narrow_width = get_js_config()->get("narrow_width")->get_int();
    this->dim_x = get_js_config()->get_int("dim_x");
    this->dim_y = get_js_config()->get_int("dim_y");
    this->router_input_queue_size = get_js_config()->get_int("router_input_queue_size");

    // Reserve the array for the target. We may have one target at each node.
    this->itf_names.resize(this->dim_x * this->dim_y);

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

            uint64_t base = config->get_uint("base");
            uint64_t size = config->get_uint("size");
            uint64_t remove_offset = config->get_uint("remove_offset");
            int x = config->get_int("x");
            int y = config->get_int("y");

            if (size > 0)
            {
                // And we add an entry so that we can turn an address into a target position
                this->entries[id].base = base;
                this->entries[id].size = size;
                this->entries[id].x = x;
                this->entries[id].y = y;
                this->entries[id].remove_offset = remove_offset;
            }

            if (x >= 0 && y >= 0)
            {
                // Once a request reaches the right position, the target will be retrieved through
                // this array indexed by the position
                this->itf_names[y * this->dim_x + x] = mapping.first;

                this->trace.msg(vp::Trace::LEVEL_DEBUG, "Adding target (name: %s, base: 0x%x, "
                    "size: 0x%x, x: %d, y: %d, remove_offset: 0x%x)\n",
                    mapping.first.c_str(), base, size, x, y, remove_offset);
            }

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

            this->network_interfaces[y*this->dim_x + x] =
                new NetworkInterface(this, x, y, this->itf_names[y * this->dim_x + x]);
        }
    }

    // Create the array of routers
    this->req_routers.resize(this->dim_x * this->dim_y);
    this->rsp_routers.resize(this->dim_x * this->dim_y);
    this->wide_routers.resize(this->dim_x * this->dim_y);


    js::Config *routers = get_js_config()->get("routers");
    if (routers != NULL)
    {
        for (js::Config *router: routers->get_elems())
        {
            int x = router->get_elem(0)->get_int();
            int y = router->get_elem(1)->get_int();

            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Adding routers (req, rsp and wide) (x: %d, y: %d)\n", x, y);

            this->req_routers[y*this->dim_x + x] = new Router(this,"req_router_", x, y, this->router_input_queue_size);
            this->rsp_routers[y*this->dim_x + x] = new Router(this, "rsp_router_", x, y, this->router_input_queue_size);
            this->wide_routers[y*this->dim_x + x] = new Router(this, "wide_router_", x, y, this->router_input_queue_size);
        }

        for (Router *router: this->req_routers)
        {
            if (router)
            {
                this->router_init_neighbours(router, this->req_routers);
            }
        }
        for (Router *router: this->rsp_routers)
        {
            if (router)
            {
                this->router_init_neighbours(router, this->rsp_routers);
            }
        }
        for (Router *router: this->wide_routers)
        {
            if (router)
            {
                this->router_init_neighbours(router, this->wide_routers);
            }
        }
    }

    for (int x=0; x<this->dim_x; x++)
    {
        for (int y=0; y<this->dim_y; y++)
        {
            NetworkInterface *ni = this->network_interfaces[y * this->dim_x + x];
            if (ni)
            {
                int r_x = x, r_y = y;
                r_x = x == 0 ? x + 1 : x == this->dim_x - 1 ? x - 1 : x;
                r_y = y == 0 ? y + 1 : y == this->dim_y - 1 ? y - 1 : y;
                ni->set_router(NetworkInterface::NW_REQ, this->req_routers[r_y*this->dim_x + r_x]);
                ni->set_router(NetworkInterface::NW_RSP, this->rsp_routers[r_y*this->dim_x + r_x]);
                ni->set_router(NetworkInterface::NW_WIDE, this->wide_routers[r_y*this->dim_x + r_x]);
            }
        }
    }
}

FlooNoc::~FlooNoc()
{
    for (Router *router: this->req_routers)
    {
        delete router;
    }
    for (Router *router: this->rsp_routers)
    {
        delete router;
    }
    for (Router *router: this->wide_routers)
    {
        delete router;
    }
    for (NetworkInterface *ni: this->network_interfaces)
    {
        delete ni;
    }
}

FloonocNode *FlooNoc::get_router_neighbour(std::vector<Router *> &routers, int x, int y)
{
    int index = y*this->dim_x + x;
    Router *router = routers[index];
    if (router)
    {
        return router;
    }
    return this->network_interfaces[index];
}

void FlooNoc::router_init_neighbours(Router *router, std::vector<Router *> &routers)
{
    router->set_neighbour(FlooNoc::DIR_RIGHT, this->get_router_neighbour(routers, router->x+1, router->y));
    router->set_neighbour(FlooNoc::DIR_LEFT, this->get_router_neighbour(routers, router->x-1, router->y));
    router->set_neighbour(FlooNoc::DIR_UP, this->get_router_neighbour(routers, router->x, router->y+1));
    router->set_neighbour(FlooNoc::DIR_DOWN, this->get_router_neighbour(routers, router->x, router->y-1));
    router->set_neighbour(FlooNoc::DIR_LOCAL, this->network_interfaces[router->y * this->dim_x + router->x]);
}



void FlooNoc::reset(bool active)
{
}



//Entry *FlooNoc::get_entry(uint64_t base, uint64_t size)
Entry *FlooNoc::get_entry(uint64_t base, uint64_t size, int x, int y)
{
    // For now, we store mapping in a classic array.
    // Just go through each entry one by one, until one is matching the requested memory location
    for (int i=0; i<this->entries.size(); i++)
    {
        Entry *entry = &this->entries[i];
        // We allow partial entry, the network interface will take care of splitting transactions
        if (base >= entry->base && base < entry->base + entry->size)
        {
            if (entry->x==0) {
                if (entry->y==y) {
                    //printf("[ZL-MOD] Spotted access to L2. Source NI is x=%d,y=%d. Destination NI is x=%d,y=%d\n",x,y,entry->x,entry->y);
                    return entry;
                }
            }
            else 
                return entry;
        }
    }
    return NULL;
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new FlooNoc(config);
}
