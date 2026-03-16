/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 * University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 * Jonas Martin, ETH (martinjo@student.ethz.ch)
 */

#include "floonoc_flex.hpp"
#include "floonoc_network_interface_flex.hpp"
#include "floonoc_router_flex.hpp"
#include <vp/itf/io.hpp>
#include <vp/vp.hpp>

FlooNoc::FlooNoc(vp::ComponentConf &config) : vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
    // Get properties from generator
    this->wide_width = get_js_config()->get("wide_width")->get_int();
    this->narrow_width = get_js_config()->get("narrow_width")->get_int();
    this->node_id = get_js_config()->get_int("node_id");
    this->router_input_queue_size =
        get_js_config()->get_int("router_input_queue_size");

    // Reserve the array for the target. We may have one target at each node.
    //
    this->itf_names.resize(this->dim_x * this->dim_y);

    this->nb_nodes = get_js_config()->get_int("nb_nodes");
    this->links = get_js_config()->get_list("links");
    this->router_degrees = get_js_config()->get_int("router_degrees");

    // Go through the mappings to create one master IO interface for each target
    js::Config *mappings = get_js_config()->get("mappings");
    if (mappings != NULL)
    {
        // For now entries are stored in a classic array. When a request is
        // received, we will to compare to each entry which may be slow when
        // having lots of target. We could optimize it by using a tree.
        this->entries.resize(mappings->get_childs().size());
        int id = 0;
        for (auto &mapping : mappings->get_childs())
        {
            // For each mapping we create the master interface where we'll
            // forward request to the target
            js::Config *config = mapping.second;

            uint64_t base = config->get_uint("base");
            uint64_t size = config->get_uint("size");
            uint64_t remove_offset = config->get_uint("remove_offset");
            int node_id = config->get_int("node_id");

            if (size > 0)
            {
                // And we add an entry so that we can turn an address into a
                // target position
                this->entries[id].base = base;
                this->entries[id].size = size;
                this->entries[id].node_id = node_id;
                this->entries[id].remove_offset = remove_offset;
            }

            if (node_id >= 0)
            {
                // Once a request reaches the right position, the target will be
                // retrieved through this array indexed by the position
                this->itf_names[y * this->dim_x + x] = mapping.first; //

                this->trace.msg(
                    vp::Trace::LEVEL_DEBUG,
                    "Adding target (name: %s, base: 0x%x, "
                    "size: 0x%x, x: %d, y: %d, remove_offset: 0x%x)\n",
                    mapping.first.c_str(), base, size, x, y, remove_offset);
            }

            id++;
        }
    }

    // Create the array of networks interfaces //TODO
    this->network_interfaces.resize(this->nb_nodes);
    js::Config *network_interfaces = get_js_config()->get("network_interfaces");
    if (network_interfaces != NULL)
    {
        for (js::Config *network_interface : network_interfaces->get_elems())
        {
            int node_id = network_interface->get_elem(0)->get_int();
            // int ... for other variables

            this->trace.msg(vp::Trace::LEVEL_DEBUG,
                            "Adding network interface (node_id: %d)\n",
                            node_id);

            this->network_interfaces[node_id] =
                new NetworkInterface(this, node_id, this->itf_names[node_id]);
        }
    }

    // Create the array of routers //TODO
    this->req_routers.resize(this->nb_nodes);
    this->rsp_routers.resize(this->nb_nodes);
    this->wide_routers.resize(this->nb_nodes);

    js::Config *routers = get_js_config()->get("routers");
    if (routers != NULL)
    {
        for (js::Config *router : routers->get_elems())
        {
            int node_id = router->get_elem(0)->get_int();
            int degree = router->get_elem(1)->get_int();
            // int ... for other variables

            this->trace.msg(
                vp::Trace::LEVEL_DEBUG,
                "Adding routers (req, rsp and wide) (node_id: %d)\n", node_id);

            this->req_routers[node_id] = new Router( // TODOs
                this, "req_router_", node_id, degree,
                this->router_input_queue_size);
            this->rsp_routers[node_id] =
                new Router(this, "rsp_router_", node_id, degree,
                           this->router_input_queue_size);
            this->wide_routers[node_id] =
                new Router(this, "wide_router_", node_id, degree,
                           this->router_input_queue_size);
        }

        for (Router *router : this->req_routers)
        {
            if (router)
            {
                this->router_init_neighbours(router, this->req_routers);
            }
        }
        for (Router *router : this->rsp_routers)
        {
            if (router)
            {
                this->router_init_neighbours(router, this->rsp_routers);
            }
        }
        for (Router *router : this->wide_routers)
        {
            if (router)
            {
                this->router_init_neighbours(router, this->wide_routers);
            }
        }
    }

    for (int x = 0; x < this->dim_x; x++) // Aaaand all of this
    {
        for (int y = 0; y < this->dim_y; y++)
        {
            NetworkInterface *ni =
                this->network_interfaces[y * this->dim_x + x];
            if (ni)
            {
                // Find the closest router starting locally
                int r_x = x, r_y = y;
                if (this->req_routers[r_y * this->dim_x + r_x] == NULL)
                {
                    r_x = x + 1;

                    if (x == this->dim_x - 1 ||
                        this->req_routers[r_y * this->dim_x + r_x] == NULL)
                    {
                        r_x = x - 1;

                        if (x == 0 ||
                            this->req_routers[r_y * this->dim_x + r_x] == NULL)
                        {
                            r_x = x;
                            r_y = y + 1;

                            if (y >= this->dim_y - 1 ||
                                this->req_routers[r_y * this->dim_x + r_x] ==
                                    NULL)
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

                ni->set_router(NetworkInterface::NW_REQ,
                               this->req_routers[r_y * this->dim_x + r_x]);
                ni->set_router(NetworkInterface::NW_RSP,
                               this->rsp_routers[r_y * this->dim_x + r_x]);
                ni->set_router(NetworkInterface::NW_WIDE,
                               this->wide_routers[r_y * this->dim_x + r_x]);
            }
        }
    }
}

FlooNoc::~FlooNoc()
{
    for (Router *router : this->req_routers)
    {
        delete router;
    }
    for (Router *router : this->rsp_routers)
    {
        delete router;
    }
    for (Router *router : this->wide_routers)
    {
        delete router;
    }
    for (NetworkInterface *ni : this->network_interfaces)
    {
        delete ni;
    }
}

// TODOS: below here
FloonocNode *FlooNoc::get_router_neighbour(std::vector<Router *> &routers,
                                           int node_id)
{
    Router *router = routers[node_id]; // Change this to look for router with
                                       // specific node_id instead of index
    if (router)
    {
        return router;
    }
    return this->network_interfaces[node_id];
}

void FlooNoc::router_init_neighbours(Router *router,
                                     std::vector<Router *> &routers)
{
    router->set_neighbour(FlooNoc::DIR_LOCAL,
                          this->network_interfaces[node_id]);
    for (int i = 0; i < links.size(); i++)
    {
        if (links[i][0] == node_id)
        {
            router->set_neighbour(FlooNoc::DIR_1, this->get_router_neighbour(
                                                      routers, links[i][1]));
        }
        else if (links[i][1] == node_id)
        {
            router->set_neighbour(FlooNoc::DIR_2, this->get_router_neighbour(
                                                      routers, links[i][0]));
        }
    }
}

void FlooNoc::reset(bool active) {}

Entry *FlooNoc::get_entry(uint64_t base, uint64_t size)
{
    // For now, we store mapping in a classic array.
    // Just go through each entry one by one, until one is matching the
    // requested memory location
    for (int i = 0; i < this->entries.size(); i++)
    {
        Entry *entry = &this->entries[i];
        // We allow partial entry, the network interface will take care of
        // splitting transactions
        if (base >= entry->base && base < entry->base + entry->size)
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