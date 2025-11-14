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

#pragma once

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

class Router;
class NetworkInterface;


class FloonocNode : public vp::Block
{
public:
    FloonocNode(Block *parent, std::string name) : vp::Block(parent, name) {}
    virtual void unstall_queue(int from_x, int from_y) = 0;
    virtual bool handle_request(FloonocNode *node, vp::IoReq *req, int from_x, int from_y) = 0;
};


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
    // Offset to be removed when request is forwarded
    uint64_t remove_offset;
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
    ~FlooNoc();

    void reset(bool active);


    // Return the memory-mapped entry corresponding to the specified mapping. Can be used to get
    // destination coordinates associated to an address location.
    Entry *get_entry(uint64_t base, uint64_t size);

    // Internal router information is stored inside the requests.
    // These constants give the indices where the information is stored in the requests data.
    static constexpr int REQ_SRC_NI = 0;     // Pointer to network interface where the request was received
    static constexpr int REQ_BURST = 1;  // Burst received from network interface
    static constexpr int REQ_DEST_X = 2;      // X coordinate of the destination target
    static constexpr int REQ_DEST_Y = 3;      // Y coordinate of the destination target
    static constexpr int REQ_WIDE = 4;        // Indicates if a request is a wide request or not. 1 for wide, 0 for narrow
    static constexpr int REQ_IS_ADDRESS = 5;     // Indicates if the request is a AR/AW request or not. 1 for address, 0 for data
    static constexpr int REQ_NB_ARGS = 6;     // Number of request data required by this model

    // The following constants gives the index in the queue array of the queue associated to each direction
    static constexpr int DIR_RIGHT = 0;
    static constexpr int DIR_LEFT = 1;
    static constexpr int DIR_UP   = 2;
    static constexpr int DIR_DOWN = 3;
    static constexpr int DIR_LOCAL = 4;

    // Width in bytes of the noc. This is used to split incoming bursts into internal requests of
    // this width so that the bandwidth corresponds to the width.
    uint64_t wide_width;
    uint64_t narrow_width;
    // X dimension of the network. This includes both routers but also targets on the edges
    int dim_x;
    // Y dimension of the network. This includes both routers but also targets on the edges
    int dim_y;

private:
    FloonocNode *get_router_neighbour(std::vector<Router *> &routers, int x, int y);
    void router_init_neighbours(Router *router, std::vector<Router *> &routers);
    FloonocNode *get_node(std::vector<Router *> &routers, int x, int y);

    // This block trace
    vp::Trace trace;
    // Set of memory-mapped entries, with one for each target. They give information about each
    // target (base address, size, position)
    std::vector<Entry> entries;
    std::vector<std::string> itf_names;
    // SIze of the routers input queues. Pushing more requests than this size will block the
    // output queue of the sender.
    int router_input_queue_size;
    // Array of routers of the noc, sorted by position from first line to last line
    std::vector<Router *> req_routers;
    std::vector<Router *> rsp_routers;
    std::vector<Router *> wide_routers;
    // Array of network interfaces of the noc, sorted by position from first line to last line
    std::vector<NetworkInterface *> network_interfaces;
};
