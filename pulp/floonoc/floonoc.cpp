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
 * Siim Rausi, ETH (srausi@student.ethz.ch)
 * Rafael Medina, ETH (medinar@iis.ee.ethz.ch)
 */

#include "floonoc.hpp"
#include "floonoc_network_interface.hpp"
#include "floonoc_router.hpp"
#include <cstring>
#include <vp/itf/io.hpp>
#include <vp/vp.hpp>

FlooNoc::FlooNoc(vp::ComponentConf &config) : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
    // Get properties from generator
    this->wide_width = this->cfg.wide_width;
    this->narrow_width = this->cfg.narrow_width;
    this->nb_nodes = this->cfg.nb_nodes;
    this->router_input_queue_size = this->cfg.router_input_queue_size;

    // Reserve the array for the target. We may have one target at each node.
    this->itf_names.resize(this->nb_nodes);

    this->router_degrees = this->cfg.router_degrees;

    for (size_t i = 0; i < this->cfg.noc_links_count; i++)
    {
        const FlooNocLinkConfig &link_cfg = this->cfg.noc_links[i];
        this->links.push_back({(int)link_cfg.src, (int)link_cfg.dst, (int)link_cfg.latency});
    }

    // Go through the mappings to create one master IO interface for each target.
    // For now entries are stored in a classic array. When a request is
    // received, we will to compare to each entry which may be slow when
    // having lots of target. We could optimize it by using a tree.
    this->entries.resize(this->cfg.noc_mappings_count);
    for (size_t id = 0; id < this->cfg.noc_mappings_count; id++)
    {
        // For each mapping we create the master interface where we'll
        // forward request to the target
        const FlooNocMappingConfig &mapping = this->cfg.noc_mappings[id];

        uint64_t base = mapping.base;
        uint64_t size = mapping.size;
        uint64_t remove_offset = mapping.remove_offset;
        int node_id = mapping.node_id;

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
            this->itf_names[node_id] = mapping.itf_name;

            this->trace.msg(
                vp::Trace::LEVEL_DEBUG,
                "Adding target (name: %s, base: 0x%x, "
                "size: 0x%x, node_id: %d, remove_offset: 0x%x)\n",
                mapping.itf_name, base, size, node_id, remove_offset);
        }
    }

    // Create the array of network interfaces (initialize empty slots to NULL)
    this->network_interfaces.resize(this->nb_nodes, NULL);
    {
        for (size_t i = 0; i < this->cfg.noc_network_interfaces_count; i++)
        {
            int node_id = this->cfg.noc_network_interfaces[i].node_id;

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

    {
        for (size_t i = 0; i < this->cfg.noc_routers_count; i++)
        {
            int node_id = this->cfg.noc_routers[i].node_id;
            int num_queues = this->cfg.noc_routers[i].num_queues;

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

    // Parse routing tables, entries which are not given are -1
    if (this->cfg.has_routing_tables)
    {
        std::vector<std::vector<int>> r_tables(this->nb_nodes,
            std::vector<int>(this->nb_nodes, -1));
        for (size_t i = 0; i < this->cfg.noc_routes_count; i++)
        {
            const FlooNocRouteConfig &route = this->cfg.noc_routes[i];
            if (route.router >= 0 && route.router < this->nb_nodes &&
                route.dest >= 0 && route.dest < this->nb_nodes)
            {
                r_tables[route.router][route.dest] = route.next_hop;
            }
        }

        for (int i = 0; i < this->nb_nodes; i++)
        {
            if (this->req_routers[i])
                this->req_routers[i]->set_routing_table(r_tables[i]);
            if (this->rsp_routers[i])
                this->rsp_routers[i]->set_routing_table(r_tables[i]);
            if (this->wide_routers[i])
                this->wide_routers[i]->set_routing_table(r_tables[i]);
        }
    }

    // Parse direction-relative routing to support default-mesh o_MAP_DIR: the neighbour of
    // each router in each direction (right, left, up, down), -1 where there is none.
    for (size_t i = 0; i < this->cfg.noc_direction_links_count; i++)
    {
        const FlooNocDirectionLinkConfig &entry = this->cfg.noc_direction_links[i];
        int node_id = entry.node_id;
        int neighbours[4] = { (int)entry.right, (int)entry.left, (int)entry.up, (int)entry.down };
        for (int dir_index = 0; dir_index < 4; dir_index++)
        {
            int neighbor_id = neighbours[dir_index];
            if (neighbor_id < 0)
            {
                continue;
            }
            if (this->req_routers[node_id])
                this->req_routers[node_id]->set_direction_neighbour(dir_index, neighbor_id);
            if (this->rsp_routers[node_id])
                this->rsp_routers[node_id]->set_direction_neighbour(dir_index, neighbor_id);
            if (this->wide_routers[node_id])
                this->wide_routers[node_id]->set_direction_neighbour(dir_index, neighbor_id);
        }
    }

    // Build the NI-to-Router map from the links array
    std::vector<int> ni_to_router_map(this->nb_nodes, -1);
    std::vector<int> ni_to_router_latency(this->nb_nodes, 1);

    for (int i = 0; i < this->links.size(); i++)
    {
        int node_a = this->links[i][0];
        int node_b = this->links[i][1];
        int latency = this->links[i][2];

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

    // Topology-specific collective fan-out policy
    std::string collective_route_algo = this->cfg.collective_route_algo;

    if (collective_route_algo != "none")
    {
        std::unordered_map<int, std::pair<int, int>> node_coords;
        // Build a map of node_id -> (x, y) coordinates for the collective routing
        for (size_t i = 0; i < this->cfg.collective_node_coords_count; i++)
        {
            const FlooNocCoordConfig &entry = this->cfg.collective_node_coords[i];
            node_coords[entry.node_id] = {(int)entry.x, (int)entry.y};
        }

        if (collective_route_algo == "xy_mesh")
        {
            this->collective_policy =
                new MeshXYCollectivePolicy(true, node_coords);
        }
        else if (collective_route_algo == "yx_mesh")
        {
            this->collective_policy =
                new MeshXYCollectivePolicy(false, node_coords);
        }
        else
        {
            this->trace.fatal(
                "Unknown collective_route_algo '%s'\n",
                collective_route_algo.c_str());
        }
    }

    this->stats.register_stat(&this->stat_collective_requests,
        "collective_requests",
        "Number of collective requests injected");
    this->stats.register_stat(&this->stat_collective_children_total,
        "collective_children_total",
        "Total forked children as part of collective requests");
    this->stats.register_stat(&this->stat_collective_reduction_merges,
        "collective_reduction_merges",
        "Total arithmetic reduction merges performed");
    this->stats.register_stat(&this->stat_collective_completed,
        "collective_completed",
        "Number of collective requests fully resolved");
    this->stats.register_stat(&this->stat_collective_completion_latency,
        "collective_completion_latency",
        "Total collective completion latencies (cycles)");
}

FlooNoc::~FlooNoc()
{
    delete this->collective_policy;

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
            int latency = this->links[i][2];

            // The neighbor is a Network Interface
            if (this->network_interfaces[neighbor_id] != NULL)
            {
                // Connect NI to next available port
                router->set_neighbour(current_port,
                                      this->network_interfaces[neighbor_id],
                                      neighbor_id, latency);
                router->set_local_ni(neighbor_id);
                current_port++;
            }
            // The neighbor is another Router
            else if (routers[neighbor_id] != NULL)
            {
                // Connect Router to next available port
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

// Resolves the completion of a collective request.
void FlooNoc::handle_collective_completion(vp::IoReq *req)
{
    void *parent_ptr = *req->arg_get(FlooNoc::REQ_PARENT);
    if (parent_ptr == NULL)
    {
        // Root request, recover the original network interface and forward the response to it
        NetworkInterface *origin_ni =
            *(NetworkInterface **)req->arg_get(FlooNoc::REQ_SRC_NI);

        int64_t inject_time =
            (int64_t)(long)*req->arg_get(FlooNoc::REQ_INJECT_TIME);
        this->stat_collective_completed++;
        this->stat_collective_completion_latency +=
            this->time.get_engine()->get_time() - inject_time;

        if (req->get_is_write())
        {
            // Notifies the burst once REQ_REM_SIZE reaches 0. 
            // Each narrow_width/wide_width chunk of a write burst is 
            // its own collective root, so replicate the check here for each chunk. 
            // Otherwise, every chunk's root would complete the whole burst on its own.
            vp::IoReq *burst = *(vp::IoReq **)req->arg_get(FlooNoc::REQ_BURST);
            *(int *)burst->arg_get_last(NetworkInterface::REQ_REM_SIZE) -=
                req->get_size();
            if (*(int *)burst->arg_get_last(NetworkInterface::REQ_REM_SIZE) !=
                0)
            {
                delete req;
                return;
            }
        }

        *req->arg_get(FlooNoc::REQ_SRC_NI) = NULL;
        origin_ni->handle_response(req);
        return;
    }

    vp::IoReq *parent = (vp::IoReq *)parent_ptr;

    if (parent->get_int(FlooNoc::REQ_COLL_OP) > FlooNoc::COLL_OP_MULTICAST)
    {
        this->stat_collective_reduction_merges++;
    }
    this->process_collective_operations(parent, req);

    delete[] req->get_data();
    delete req;

    int pending = parent->get_int(FlooNoc::REQ_PEND_KIDS) - 1;
    parent->set_int(FlooNoc::REQ_PEND_KIDS, pending);
    if (pending <= 0)
    {
        this->handle_collective_completion(parent);
    }
}

// Handles one independent write contribution to a write-triggered
// reduction/barrier converging on `req`'s address
bool FlooNoc::handle_write_reduction_contribution(vp::IoReq *req, vp::IoReq **committed_req)
{
    int op = req->get_int(FlooNoc::REQ_COLL_OP);
    uint64_t addr = req->get_addr();
    uint64_t mask = (uint64_t)(long)*req->arg_get(FlooNoc::REQ_COLL_MASK);

    PendingWriteReduction *group = NULL;
    for (PendingWriteReduction &candidate : this->pending_write_reductions)
    {
        if (candidate.addr == addr && candidate.op == op && candidate.mask == mask)
        {
            group = &candidate;
            break;
        }
    }

    if (group == NULL)
    {
        this->pending_write_reductions.push_back({});
        group = &this->pending_write_reductions.back();
        group->addr = addr;
        group->op = op;
        group->mask = mask;
        group->accumulator = req;
        group->expected = this->collective_policy->member_count(
            req->get_int(FlooNoc::REQ_SRC_ID), mask);
        group->received = 1;
    }
    else
    {
        this->process_collective_operations(group->accumulator, req);
        group->others.push_back(req);
        group->received++;
        this->stat_collective_reduction_merges++;
    }

    if (group->received < group->expected)
    {
        return false;
    }

    // Group complete. Park the other contributions here and let
    // NetworkInterface::handle_response() release them (via a
    // reduction-ack echo) once the accumulator's own ack actually fires
    this->pending_reduction_acks.push_back(
        {group->accumulator, addr, mask, std::move(group->others)});

    int64_t inject_time = (int64_t)(long)*req->arg_get(FlooNoc::REQ_INJECT_TIME);
    this->stat_collective_completed++;
    this->stat_collective_completion_latency +=
        this->time.get_engine()->get_time() - inject_time;

    *committed_req = group->accumulator;
    *group = this->pending_write_reductions.back();
    this->pending_write_reductions.pop_back();
    return true;
}

// True once `accumulator`'s group has other contributors parked waiting
// to be released
bool FlooNoc::has_pending_reduction_others(vp::IoReq *accumulator)
{
    for (PendingReductionAck &group : this->pending_reduction_acks)
    {
        if (group.accumulator == accumulator)
        {
            return !group.others.empty();
        }
    }
    return false;
}

// Returns (and forgets) the one contribution parked for (addr, mask) that
// was made by `node_id`, or nullptr if `node_id` has none (e.g. it is the
// group's own accumulator/destination)
vp::IoReq *FlooNoc::take_pending_reduction_other(uint64_t addr, uint64_t mask, int node_id)
{
    for (size_t i = 0; i < this->pending_reduction_acks.size(); i++)
    {
        PendingReductionAck &group = this->pending_reduction_acks[i];
        if (group.addr != addr || group.mask != mask)
        {
            continue;
        }

        for (size_t j = 0; j < group.others.size(); j++)
        {
            if (group.others[j]->get_int(FlooNoc::REQ_SRC_ID) == node_id)
            {
                vp::IoReq *other = group.others[j];
                group.others[j] = group.others.back();
                group.others.pop_back();
                if (group.others.empty())
                {
                    this->pending_reduction_acks[i] = std::move(this->pending_reduction_acks.back());
                    this->pending_reduction_acks.pop_back();
                }
                return other;
            }
        }
        return NULL;
    }
    return NULL;
}

// Walks up the fork tree freeing a reduction-ack echo packet (and its
// ancestors) once all of its forked copies have been delivered
void FlooNoc::handle_reduction_ack_echo_completion(vp::IoReq *req)
{
    void *parent_ptr = *req->arg_get(FlooNoc::REQ_PARENT);
    delete[] req->get_data();
    delete req;

    if (parent_ptr == NULL)
    {
        return;
    }

    vp::IoReq *parent = (vp::IoReq *)parent_ptr;
    int pending = parent->get_int(FlooNoc::REQ_PEND_KIDS) - 1;
    parent->set_int(FlooNoc::REQ_PEND_KIDS, pending);
    if (pending <= 0)
    {
        this->handle_reduction_ack_echo_completion(parent);
    }
}

void FlooNoc::init_reduction_identity(vp::IoReq *req)
{
    int collective_op = req->get_int(FlooNoc::REQ_COLL_OP);
    if (collective_op <= FlooNoc::COLL_OP_MULTICAST)
    {
        return;
    }

    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();

    switch (collective_op)
    {
        case FlooNoc::COLL_OP_LSBAND:
            std::memset(data, 0xFF, size);
            break;

        case FlooNoc::COLL_OP_REDADD_UINT16:
        case FlooNoc::COLL_OP_REDADD_INT16:
        case FlooNoc::COLL_OP_REDADD_FP16:
            std::memset(data, 0, size);
            break;

        case FlooNoc::COLL_OP_REDMAX_UINT16:
            std::memset(data, 0x00, size);
            break;

        case FlooNoc::COLL_OP_REDMAX_INT16:
        {
            int16_t *dst = (int16_t *)data;
            for (uint64_t i = 0; i < size / sizeof(int16_t); i++)
            {
                dst[i] = INT16_MIN;
            }
            break;
        }

        case FlooNoc::COLL_OP_REDMAX_FP16:
        {
            uint16_t *dst = (uint16_t *)data;
            flexfloat_t neg_inf;
            ff_init(&neg_inf, FlooNoc::fp16_desc);
            flexfloat_set_bits(&neg_inf, 0xFC00); // fp16 -inf
            uint16_t bits = flexfloat_get_bits(&neg_inf);
            for (uint64_t i = 0; i < size / sizeof(uint16_t); i++)
            {
                dst[i] = bits;
            }
            break;
        }

        default:
            this->trace.fatal(
                "init_reduction_identity with unsupported REQ_COLL_OP %d\n",
                collective_op);
    }
}

// Merges `req` (a completed forked collective child) into `parent`
// (either the collective's root, or an intermediate fork point one level
// up) according to `parent`'s REQ_COLL_OP
void FlooNoc::process_collective_operations(vp::IoReq *parent, vp::IoReq *req)
{
    int collective_op = parent->get_int(FlooNoc::REQ_COLL_OP);
    uint64_t size = req->get_size();

    switch (collective_op)
    {
        case FlooNoc::COLL_OP_MULTICAST:
            // Broadcast: nothing to combine
            break;

        case FlooNoc::COLL_OP_LSBAND:
        {
            // Barrier/wakeup: AND bit 0 of every participant's data byte by byte
            uint8_t *dst = parent->get_data();
            const uint8_t *src = req->get_data();
            for (uint64_t i = 0; i < size; i++)
            {
                dst[i] = (dst[i] & 1) & (src[i] & 1);
            }
            break;
        }

        case FlooNoc::COLL_OP_REDADD_UINT16:
        {
            uint16_t *dst = (uint16_t *)parent->get_data();
            uint16_t *src = (uint16_t *)req->get_data();
            for (uint64_t i = 0; i < size / sizeof(uint16_t); i++)
            {
                dst[i] = dst[i] + src[i];
            }
            break;
        }
        case FlooNoc::COLL_OP_REDADD_INT16:
        {
            int16_t *dst = (int16_t *)parent->get_data();
            int16_t *src = (int16_t *)req->get_data();
            for (uint64_t i = 0; i < size / sizeof(int16_t); i++)
            {
                dst[i] = dst[i] + src[i];
            }
            break;
        }
        case FlooNoc::COLL_OP_REDADD_FP16:
        {
            uint16_t *dst = (uint16_t *)parent->get_data();
            uint16_t *src = (uint16_t *)req->get_data();
            for (uint64_t i = 0; i < size / sizeof(uint16_t); i++)
            {
                flexfloat_t a, b, result;
                ff_init(&a, FlooNoc::fp16_desc);
                ff_init(&b, FlooNoc::fp16_desc);
                ff_init(&result, FlooNoc::fp16_desc);
                flexfloat_set_bits(&a, dst[i]);
                flexfloat_set_bits(&b, src[i]);
                ff_add(&result, &a, &b);
                dst[i] = (uint16_t)flexfloat_get_bits(&result);
            }
            break;
        }
        case FlooNoc::COLL_OP_REDMAX_UINT16:
        {
            uint16_t *dst = (uint16_t *)parent->get_data();
            uint16_t *src = (uint16_t *)req->get_data();
            for (uint64_t i = 0; i < size / sizeof(uint16_t); i++)
            {
                dst[i] = dst[i] > src[i] ? dst[i] : src[i];
            }
            break;
        }
        case FlooNoc::COLL_OP_REDMAX_INT16:
        {
            int16_t *dst = (int16_t *)parent->get_data();
            int16_t *src = (int16_t *)req->get_data();
            for (uint64_t i = 0; i < size / sizeof(int16_t); i++)
            {
                dst[i] = dst[i] > src[i] ? dst[i] : src[i];
            }
            break;
        }
        case FlooNoc::COLL_OP_REDMAX_FP16:
        {
            uint16_t *dst = (uint16_t *)parent->get_data();
            uint16_t *src = (uint16_t *)req->get_data();
            for (uint64_t i = 0; i < size / sizeof(uint16_t); i++)
            {
                flexfloat_t a, b, result;
                ff_init(&a, FlooNoc::fp16_desc);
                ff_init(&b, FlooNoc::fp16_desc);
                ff_init(&result, FlooNoc::fp16_desc);
                flexfloat_set_bits(&a, dst[i]);
                flexfloat_set_bits(&b, src[i]);
                ff_max(&result, &a, &b);
                dst[i] = (uint16_t)flexfloat_get_bits(&result);
            }
            break;
        }
        default:
            this->trace.fatal(
                "Collective completion with unsupported REQ_COLL_OP %d\n",
                collective_op);
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new FlooNoc(config);
}