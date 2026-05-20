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

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "floonoc_v2.hpp"
#include "floonoc_router_v2.hpp"
#include "floonoc_network_interface_v2.hpp"


FlooNocV2::FlooNocV2(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
    this->wide_width = get_js_config()->get("wide_width")->get_int();
    this->narrow_width = get_js_config()->get("narrow_width")->get_int();
    this->dim_x = get_js_config()->get_int("dim_x");
    this->dim_y = get_js_config()->get_int("dim_y");
    this->router_input_queue_size = get_js_config()->get_int("router_input_queue_size");

    this->itf_names.resize(this->dim_x * this->dim_y);

    js::Config *mappings = get_js_config()->get("mappings");
    if (mappings != NULL)
    {
        this->entries.resize(mappings->get_childs().size());
        int id = 0;
        for (auto& mapping: mappings->get_childs())
        {
            js::Config *config = mapping.second;

            uint64_t base = config->get_uint("base");
            uint64_t size = config->get_uint("size");
            uint64_t remove_offset = config->get_uint("remove_offset");
            int x = config->get_int("x");
            int y = config->get_int("y");

            if (size > 0)
            {
                this->entries[id].base = base;
                this->entries[id].size = size;
                this->entries[id].x = x;
                this->entries[id].y = y;
                this->entries[id].remove_offset = remove_offset;
            }

            if (x >= 0 && y >= 0)
            {
                this->itf_names[y * this->dim_x + x] = mapping.first;

                this->trace.msg(vp::Trace::LEVEL_DEBUG, "Adding target (name: %s, base: 0x%x, "
                    "size: 0x%x, x: %d, y: %d, remove_offset: 0x%x)\n",
                    mapping.first.c_str(), base, size, x, y, remove_offset);
            }

            id++;
        }
    }

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
                new NetworkInterfaceV2(this, x, y, this->itf_names[y * this->dim_x + x]);
        }
    }

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

            this->req_routers[y*this->dim_x + x] = new RouterV2(this, "req_router_", x, y, this->router_input_queue_size);
            this->rsp_routers[y*this->dim_x + x] = new RouterV2(this, "rsp_router_", x, y, this->router_input_queue_size);
            this->wide_routers[y*this->dim_x + x] = new RouterV2(this, "wide_router_", x, y, this->router_input_queue_size);
        }

        for (RouterV2 *router: this->req_routers)
        {
            if (router)
            {
                this->router_init_neighbours(router, this->req_routers);
            }
        }
        for (RouterV2 *router: this->rsp_routers)
        {
            if (router)
            {
                this->router_init_neighbours(router, this->rsp_routers);
            }
        }
        for (RouterV2 *router: this->wide_routers)
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
            NetworkInterfaceV2 *ni = this->network_interfaces[y * this->dim_x + x];
            if (ni)
            {
                int r_x = x, r_y = y;
                if (this->req_routers[r_y*this->dim_x + r_x] == NULL)
                {
                    r_x = x + 1;

                    if (x == this->dim_x - 1 || this->req_routers[r_y*this->dim_x + r_x] == NULL)
                    {
                        r_x = x - 1;

                        if (x == 0 || this->req_routers[r_y*this->dim_x + r_x] == NULL)
                        {
                            r_x = x;
                            r_y = y + 1;

                            if (y >= this->dim_y - 1 || this->req_routers[r_y*this->dim_x + r_x] == NULL)
                            {
                                r_y = y - 1;
                                if (y == 0)
                                {
                                    r_x = x;
                                    r_y = y;
                                }
                            }
                        }
                    }
                }

                ni->set_router(NetworkInterfaceV2::NW_REQ, this->req_routers[r_y*this->dim_x + r_x]);
                ni->set_router(NetworkInterfaceV2::NW_RSP, this->rsp_routers[r_y*this->dim_x + r_x]);
                ni->set_router(NetworkInterfaceV2::NW_WIDE, this->wide_routers[r_y*this->dim_x + r_x]);
            }
        }
    }
}

FlooNocV2::~FlooNocV2()
{
    for (RouterV2 *router: this->req_routers)
    {
        delete router;
    }
    for (RouterV2 *router: this->rsp_routers)
    {
        delete router;
    }
    for (RouterV2 *router: this->wide_routers)
    {
        delete router;
    }
    for (NetworkInterfaceV2 *ni: this->network_interfaces)
    {
        delete ni;
    }
}

FloonocNodeV2 *FlooNocV2::get_router_neighbour(std::vector<RouterV2 *> &routers, int x, int y)
{
    int index = y*this->dim_x + x;
    RouterV2 *router = routers[index];
    if (router)
    {
        return router;
    }
    return this->network_interfaces[index];
}

void FlooNocV2::router_init_neighbours(RouterV2 *router, std::vector<RouterV2 *> &routers)
{
    router->set_neighbour(FlooNocV2::DIR_RIGHT, this->get_router_neighbour(routers, router->x+1, router->y));
    router->set_neighbour(FlooNocV2::DIR_LEFT, this->get_router_neighbour(routers, router->x-1, router->y));
    router->set_neighbour(FlooNocV2::DIR_UP, this->get_router_neighbour(routers, router->x, router->y+1));
    router->set_neighbour(FlooNocV2::DIR_DOWN, this->get_router_neighbour(routers, router->x, router->y-1));
    router->set_neighbour(FlooNocV2::DIR_LOCAL, this->network_interfaces[router->y * this->dim_x + router->x]);
}


void FlooNocV2::reset(bool active)
{
}


EntryV2 *FlooNocV2::get_entry(uint64_t base, uint64_t size)
{
    for (int i=0; i<this->entries.size(); i++)
    {
        EntryV2 *entry = &this->entries[i];
        if (base >= entry->base && base < entry->base + entry->size)
        {
            return entry;
        }
    }
    return NULL;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new FlooNocV2(config);
}
