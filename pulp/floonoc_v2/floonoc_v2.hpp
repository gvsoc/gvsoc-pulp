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

#pragma once

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>

class RouterV2;
class NetworkInterfaceV2;
class FloonocReqV2;


class FloonocNodeV2 : public vp::Block
{
public:
    FloonocNodeV2(Block *parent, std::string name) : vp::Block(parent, name) {}
    virtual void unstall_queue(int from_x, int from_y) = 0;
    virtual bool handle_request(FloonocNodeV2 *node, FloonocReqV2 *req, int from_x, int from_y) = 0;
};


/**
 * Subclass of v2 vp::IoReq used to carry per-mesh metadata that the v1 model
 * stored in the IoReq arg-stack. v2 has no arg stack, so we attach the data as
 * regular fields on a subclass that the NI allocates and the router downcasts.
 */
class FloonocReqV2 : public vp::IoReq
{
public:
    // Destination position in the mesh
    int dest_x;
    int dest_y;
    // Pointer to the originating NI. NULL means the request is travelling on the
    // response path (back to the source NI).
    NetworkInterfaceV2 *src_ni;
    // Pointer back to the external IoReq (from the master) that this internal
    // request belongs to.
    vp::IoReq *burst;
    // True if the request travels on the wide network, false for narrow.
    bool wide;
    // True if this is the AR/AW (address) phase of a split request, false if it
    // is the data phase.
    bool is_address;
    // Pre-translation address, used for VCD traces in the routers.
    uint64_t initiator_addr;
};


/**
 * Memory-map entry: range -> target position on the mesh.
 */
class EntryV2
{
public:
    uint64_t base;
    uint64_t size;
    int x;
    int y;
    uint64_t remove_offset;
};


/**
 * v2 FlooNoC top component.
 *
 * Structurally identical to v1 FlooNoc, but built on top of the v2 io protocol.
 * Instantiated indirectly through the floonoc_v2_new() factory called from the
 * shared gv_new in floonoc.cpp when use_v2_ni is set.
 */
class FlooNocV2 : public vp::Component
{
public:
    FlooNocV2(vp::ComponentConf &config);
    ~FlooNocV2();

    void reset(bool active);

    EntryV2 *get_entry(uint64_t base, uint64_t size);

    static constexpr int DIR_RIGHT = 0;
    static constexpr int DIR_LEFT = 1;
    static constexpr int DIR_UP   = 2;
    static constexpr int DIR_DOWN = 3;
    static constexpr int DIR_LOCAL = 4;

    uint64_t wide_width;
    uint64_t narrow_width;
    int dim_x;
    int dim_y;

private:
    FloonocNodeV2 *get_router_neighbour(std::vector<RouterV2 *> &routers, int x, int y);
    void router_init_neighbours(RouterV2 *router, std::vector<RouterV2 *> &routers);
    FloonocNodeV2 *get_node(std::vector<RouterV2 *> &routers, int x, int y);

    vp::Trace trace;
    std::vector<EntryV2> entries;
    std::vector<std::string> itf_names;
    int router_input_queue_size;
    std::vector<RouterV2 *> req_routers;
    std::vector<RouterV2 *> rsp_routers;
    std::vector<RouterV2 *> wide_routers;
    std::vector<NetworkInterfaceV2 *> network_interfaces;
};
