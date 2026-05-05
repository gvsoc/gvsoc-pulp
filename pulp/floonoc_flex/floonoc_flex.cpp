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
    this->nb_nodes = get_js_config()->get_int("nb_nodes");
    this->router_input_queue_size =
        get_js_config()->get_int("router_input_queue_size");

    // Reserve the array for the target. We may have one target at each node.
    //
    this->itf_names.resize(this->nb_nodes);

    this->nb_nodes = get_js_config()->get_int("nb_nodes");
    this->router_degrees = get_js_config()->get_int("router_degrees");

    js::Config *links_cfg = get_js_config()->get("links");
    if (links_cfg != NULL)
    {
        for (js::Config *link_cfg : links_cfg->get_elems())
        {
            std::vector<int> link;
            for (js::Config *elem : link_cfg->get_elems())
            {
                link.push_back(elem->get_int());
            }
            this->links.push_back(link);
        }
    }

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

            if (node_id >= 0 && node_id < this->nb_nodes)
            {
                // Once a request reaches the right position, the target will be
                // retrieved through this array indexed by the position
                this->itf_names[node_id] = mapping.first;

                this->trace.msg(
                    vp::Trace::LEVEL_DEBUG,
                    "Adding target (name: %s, base: 0x%x, "
                    "size: 0x%x, node_id: %d, remove_offset: 0x%x)\n",
                    mapping.first.c_str(), base, size, node_id, remove_offset);
            }

            id++;
        }
    }

    // Create the array of networks interfaces (initialize empty slots to NULL)
    this->network_interfaces.resize(this->nb_nodes, NULL);
    js::Config *network_interfaces = get_js_config()->get("network_interfaces");
    if (network_interfaces != NULL)
    {
        for (js::Config *network_interface : network_interfaces->get_elems())
        {
            int node_id = network_interface->get_elem(0)->get_int();

            this->trace.msg(vp::Trace::LEVEL_DEBUG,
                            "Adding network interface (node_id: %d)\n",
                            node_id);

            this->network_interfaces[node_id] =
                new NetworkInterface(this, node_id, this->itf_names[node_id]);
        }
    }

    // Create sparse vectors of routers
    this->req_routers.resize(this->nb_nodes, NULL);
    this->rsp_routers.resize(this->nb_nodes, NULL);
    this->wide_routers.resize(this->nb_nodes, NULL);

    js::Config *routers = get_js_config()->get("routers");
    if (routers != NULL)
    {
        for (js::Config *router : routers->get_elems())
        {
            int node_id = router->get_elem(0)->get_int();
            int num_queues = router->get_elem(1)->get_int();
            // int ... for other variables

            this->trace.msg(
                vp::Trace::LEVEL_DEBUG,
                "Adding routers (req, rsp and wide) (node_id: %d)\n", node_id);

            this->req_routers[node_id] =
                new Router(this, "req_router_", node_id, num_queues,
                           this->router_input_queue_size);
            this->rsp_routers[node_id] =
                new Router(this, "rsp_router_", node_id, num_queues,
                           this->router_input_queue_size);
            this->wide_routers[node_id] =
                new Router(this, "wide_router_", node_id, num_queues,
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

    // Parse routing tables
    js::Config *routing_tables_cfg = get_js_config()->get("routing_tables");
    if (routing_tables_cfg != NULL)
    {
        for (int i = 0; i < this->nb_nodes; i++)
        {
            js::Config *router_table_cfg =
                routing_tables_cfg->get(std::to_string(i));
            std::vector<int> r_table(this->nb_nodes, -1);

            if (router_table_cfg != NULL)
            {
                for (int dest = 0; dest < this->nb_nodes; dest++)
                {
                    js::Config *hop =
                        router_table_cfg->get(std::to_string(dest));
                    if (hop)
                        r_table[dest] = hop->get_int();
                }
            }

            if (this->req_routers[i])
                this->req_routers[i]->set_routing_table(r_table);
            if (this->rsp_routers[i])
                this->rsp_routers[i]->set_routing_table(r_table);
            if (this->wide_routers[i])
                this->wide_routers[i]->set_routing_table(r_table);
        }
    }

    // Build the NI-to-Router map from the links array
    std::vector<int> ni_to_router_map(this->nb_nodes, -1);
    std::vector<int> ni_to_router_latency(this->nb_nodes, 1);

    for (int i = 0; i < this->links.size(); i++)
    {
        int node_a = this->links[i][0];
        int node_b = this->links[i][1];
        int latency =
            this->links[i][2]; // Latency is guaranteed to be the third element

        // If Node A is an NI and Node B is a Router
        if (this->network_interfaces[node_a] != NULL &&
            this->req_routers[node_b] != NULL)
        {
            ni_to_router_map[node_a] = node_b;
            ni_to_router_latency[node_a] = latency;
        }
        // If Node B is an NI and Node A is a Router
        else if (this->network_interfaces[node_b] != NULL &&
                 this->req_routers[node_a] != NULL)
        {
            ni_to_router_map[node_b] = node_a;
            ni_to_router_latency[node_b] = latency;
        }
    }

    // Assign the discovered upstream routers to the NIs
    for (int i = 0; i < this->nb_nodes; i++)
    {
        NetworkInterface *ni = this->network_interfaces[i];

        if (ni)
        {
            int target_router_id = ni_to_router_map[i];
            int latency = ni_to_router_latency[i];

            if (target_router_id != -1 &&
                this->req_routers[target_router_id] != NULL)
            {
                ni->set_router(NetworkInterface::NW_REQ,
                               this->req_routers[target_router_id], latency);
                ni->set_router(NetworkInterface::NW_RSP,
                               this->rsp_routers[target_router_id], latency);
                ni->set_router(NetworkInterface::NW_WIDE,
                               this->wide_routers[target_router_id], latency);
            }
            else
            {
                this->trace.msg(
                    vp::Trace::LEVEL_ERROR,
                    "NI %d has no valid router assigned in the links array!\n",
                    i);
            }
        }
    }
}

FlooNoc::~FlooNoc()
{
    // === PRINT PERFORMANCE REPORT ===
    printf(
        "\n===============================================================\n");
    printf("                  FlooNoC Performance Report                   \n");
    printf("===============================================================\n");

    printf("\n--- Network Interfaces (Traffic Load) ---\n");
    printf(" Node ID | Injected Packets | Received Responses\n");
    printf("---------------------------------------------------------------\n");
    for (NetworkInterface *ni : this->network_interfaces)
    {
        if (ni)
        {
            printf("   %3d   | %16lu | %18lu\n", ni->get_id(),
                   ni->stat_injected_packets, ni->stat_received_responses);
        }
    }

    printf("\n--- WIDE Routers (Data Routing & Congestion) ---\n");
    printf(" Node ID | Routed Packets | Stalled Cycles | Congestion Rate \n");
    printf("---------------------------------------------------------------\n");

    for (Router *router : this->wide_routers)
    {
        if (router)
        {
            double congestion = 0.0;
            if (router->stat_routed_packets + router->stat_stall_cycles > 0)
            {
                congestion =
                    (double)router->stat_stall_cycles /
                    (router->stat_routed_packets + router->stat_stall_cycles) *
                    100.0;
            }
            printf("   %3d   | %14lu | %14lu | %13.2f %%\n", router->node_id,
                   router->stat_routed_packets, router->stat_stall_cycles,
                   congestion);
        }
    }
    printf("===============================================================\n");

    // Calculate and print the global average packet latency in cycles
    uint64_t global_latency_cycles = 0;
    uint64_t global_arrived_pkts = 0;
    for (NetworkInterface *ni : this->network_interfaces)
    {
        if (ni)
        {
            global_latency_cycles += ni->stat_total_packet_latency;
            global_arrived_pkts += ni->stat_arrived_packets;
        }
    }

    double global_avg_lat_cycles =
        global_arrived_pkts > 0
            ? ((double)global_latency_cycles / global_arrived_pkts)
            : 0.0;

    printf("\nGlobal Average Packet Latency: %.2f cycles\n",
           global_avg_lat_cycles);
    printf(
        "===============================================================\n\n");

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

void FlooNoc::router_init_neighbours(Router *router,
                                     std::vector<Router *> &routers)
{
    int node_id = router->node_id;
    int current_port = 0;

    for (int i = 0; i < this->links.size(); i++)
    {
        int neighbor_id = -1;

        // Check if this link involves our router
        if (this->links[i][0] == node_id)
        {
            neighbor_id = this->links[i][1];
        }
        else if (this->links[i][1] == node_id)
        {
            neighbor_id = this->links[i][0];
        }

        if (neighbor_id != -1)
        {
            int latency =
                this->links[i]
                           [2]; // Latency is guaranteed to be the third element

            // The neighbor is a Network Interface
            if (this->network_interfaces[neighbor_id] != NULL)
            {
                // RIGHT NOW THIS CONDITION IS SAME AS OTHER ONE but could add
                // different behaviour later on
                router->set_neighbour(current_port,
                                      this->network_interfaces[neighbor_id],
                                      neighbor_id, latency);
                current_port++;
            }
            // The neighbor is another Router
            else if (routers[neighbor_id] != NULL)
            {
                // Plug the Router into the next available routing port and
                // register its ID
                router->set_neighbour(current_port, routers[neighbor_id],
                                      neighbor_id, latency);
                current_port++;
            }
        }
    }

    if (current_port > router->num_queues + 1)
    {
        this->trace.msg(
            vp::Trace::LEVEL_ERROR,
            "Router %d has %d queues, %d connected (including local)\n",
            node_id, router->num_queues, current_port);
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