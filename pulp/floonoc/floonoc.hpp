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

#pragma once

#include <vp/vp.hpp>

class Router;
class NetworkInterface;


/**
 * @brief FlooNoc memory-map entry
 *
 * Each entry represents a memory range for which all request targetting this range should
 * be forwarded to the associated target represented by a position.
 */
class Entry
{
public:
    // Base address of the entry
    uint64_t base;
    // Size of the entry
    uint64_t size;
    // X position of the target where requests to this mapping should be forwarded
    int x;
    // Y position of the target where requests to this mapping should be forwarded
    int y;
    // Z position of the target where requests to this mapping should be forwarded (3D NoC only)
    int z;
};



/**
 * @brief FlooNoc network-on-chip
 *
 * This models the FlooNoc as a single components.
 * It has to be connected to initiators and targets which are accessible through memory-mapped
 * requests.
 * Internals routers and network interfaces are declared by the generator in the component
 * configuration and instantiated as internal blocks.
 * Here are the definition of few terms used in the model:
 * - Network interface: entry point where bursts from initiators are injected into the noc. It is
 *   in charge of determining the position of the destination based on the destination address, and
 *   splitting bursts into internal noc requests which fits the width of the noc.
 * - Routers: their role is to bring internal requests from one point to another. There should be
 *   one router at each node of the noc, so that requests can go anywhere.
 * - Targets: a target is a master IO interface which corresponds to final destinations for
 *   internal requests. Once the destination is reached by a router, the request is sent to the
 *   target by sending the request to the interface.
 */
class FlooNoc : public vp::Component
{
public:
    FlooNoc(vp::ComponentConf &config);

    void reset(bool active);

    // Return the router at specified position
    Router *get_router(int x, int y, int z = 0);
    // Return the target at specified position
    vp::IoMaster *get_target(int x, int y, int z = 0);
    // Return the network interface at specified position
    NetworkInterface *get_network_interface(int x, int y, int z = 0);
    // Return the memory-mapped entry corresponding to the specified mapping. Can be used to get
    // destination coordinates associated to an address location.
    Entry *get_entry(uint64_t base, uint64_t size);
    // Can be called to notify that an asynchronous response to a request was received. The noc
    // will then call the initiating network interface so that it is handled by the burst.
    void handle_request_end(vp::IoReq *req);

    // Internal router information is stored inside the requests.
    // These constants give the indices where the information is stored in the requests data.
    static constexpr int REQ_DEST_NI = 0;     // Pointer to network interface where the request was received
    static constexpr int REQ_DEST_BURST = 1;  // Burst received from network interface
    static constexpr int REQ_DEST_BASE = 2;   // Base address of the destination target
    static constexpr int REQ_DEST_X = 3;      // X coordinate of the destination target
    static constexpr int REQ_DEST_Y = 4;      // Y coordinate of the destination target
    static constexpr int REQ_ROUTER = 5;      // When a request is stalled, this gives the router where to grant it
    static constexpr int REQ_QUEUE = 6;       // When a request is stalled, this gives the queue where to grant it
    static constexpr int REQ_SRC_X = 7;       // X coordinate of the source target
    static constexpr int REQ_SRC_Y = 8;       // Y coordinate of the source target
    static constexpr int REQ_PARENT = 9;      // [Collective Only] If it is a request from a router internally
    static constexpr int REQ_COLL_TYPE = 10;      // [Collective Only] If it is a request from a router internally
    static constexpr int REQ_ROW_MASK = 11;   // [Collective Only] The row mask for collecitive primitives
    static constexpr int REQ_COL_MASK = 12;   // [Collective Only] The col mask for collecitive primitives
    static constexpr int REQ_PEND_KIDS = 13;   // [Collective Only] How many kids to be waited for response
    static constexpr int REQ_MOMENTUM = 14;      // [Collective Only] Tree direction
    static constexpr int REQ_DEST_Z = 15;      // Z coordinate of the destination target
    static constexpr int REQ_SRC_Z = 16;      // Z coordinate of the destination target
    static constexpr int REQ_LAY_MASK = 17;   // [Collective Only] The layer mask for collecitive primitives
    static constexpr int REQ_NB_ARGS = 18;    // Number of request data required by this model

    // The following constants gives the index in the queue array of the queue associated to each direction
    static constexpr int DIR_RIGHT = 0;
    static constexpr int DIR_LEFT = 1;
    static constexpr int DIR_UP   = 2;
    static constexpr int DIR_DOWN = 3;
    static constexpr int DIR_LOCAL = 4;
    // 3D extension directions
    static constexpr int DIR_ZPLUS = 5;
    static constexpr int DIR_ZMINUS = 6;

    // [Collective Only]
    static constexpr int MOMENTUM_RIGHT = 0;
    static constexpr int MOMENTUM_LEFT  = 1;
    static constexpr int MOMENTUM_UP    = 2;
    static constexpr int MOMENTUM_DOWN  = 3;
    static constexpr int MOMENTUM_ZERO  = 4;
    // TODO: What is momentum? How to z extend?
    static constexpr int MOMENTUM_ZPLUS  = 5;
    static constexpr int MOMENTUM_ZMINUS = 6;

    // Width in bytes of the noc. This is used to split incoming bursts into internal requests of
    // this width so that the bandwidth corresponds to the width.
    uint64_t width;

    // Whether Support Atomics
    bool atomics;

    // Whether Support Collective
    bool collective;

    // Whether Support HBM node aliasing
    uint64_t edge_node_alias;
    uint64_t edge_node_alias_start_bit;

    // Whether Support Interleaving
    uint64_t interleave_enable;
    uint64_t interleave_region_base;
    uint64_t interleave_region_size;
    uint64_t interleave_granularity;
    uint64_t interleave_bit_start;
    uint64_t interleave_bit_width;

    // X dimension of the network. This includes both routers but also targets on the edges
    int dim_x;
    // Y dimension of the network. This includes both routers but also targets on the edges
    int dim_y;
    // Z dimension of the network. This includes both routers but also targets on the edges
    int dim_z;

private:
    // Callback called when a target request is asynchronously granted after a denied error was
    // reported
    static void grant(vp::Block *__this, vp::IoReq *req);
    // Callback called when a target request is asynchronously replied after a pending error was
    // reported
    static void response(vp::Block *__this, vp::IoReq *req);

    // This block trace
    vp::Trace trace;
    // Set of memory-mapped entries, with one for each target. They give information about each
    // target (base address, size, position)
    std::vector<Entry> entries;
    // SIze of the routers input queues. Pushing more requests than this size will block the
    // output queue of the sender.
    int router_input_queue_size;
    // Array of routers of the noc, sorted by position from first line to last line
    std::vector<Router *> routers;
    // Array of targets of the noc, sorted by position from first line to last line. This contains
    // both the targets on the edges and the targets at each node
    std::vector<vp::IoMaster *> targets;
    // Array of network interfaces of the noc, sorted by position from first line to last line
    std::vector<NetworkInterface *> network_interfaces;
};
