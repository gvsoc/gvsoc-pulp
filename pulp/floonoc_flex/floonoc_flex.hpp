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
 *          Siim Rausi, ETH (srausi@student.ethz.ch)
 */

#pragma once

#include "floonoc_collective_policy.hpp"
#include <cpu/iss/flexfloat/flexfloat.h>
#include <vp/itf/io.hpp>
#include <vp/signal.hpp>

class Router;
class NetworkInterface;

class FloonocNode : public vp::Block
{
  public:
    FloonocNode(Block *parent, std::string name) : vp::Block(parent, name) {}
    virtual void unstall_queue(int from_node) = 0;
    virtual bool handle_request(FloonocNode *node, vp::IoReq *req,
                                int from_node)
    {
        return false;
    }
};

/**
 * @brief FlooNoc memory-map entry
 *
 * Each entry represents a memory range for which all request targetting this
 * range should be forwarded to the associated target represented by a position.
 */
class Entry
{
  public:
    // Base address of the entry
    uint64_t base;
    // Size of the entry
    uint64_t size;
    // Node ID of the target where requests to this mapping should be
    // forwarded
    int node_id;
    // Offset to be removed when request is forwarded
    uint64_t remove_offset;
};

/**
 * @brief FlooNoc network-on-chip
 *
 * This models the FlooNoc as a single components.
 * It has to be connected to initiators and targets which are accessible through
 * memory-mapped requests. Internals routers and network interfaces are declared
 * by the generator in the component configuration and instantiated as internal
 * blocks. Here are the definition of few terms used in the model:
 * - Network interface: entry point where bursts from initiators are injected
 * into the noc. It is in charge of determining the position of the destination
 * based on the destination address, and splitting bursts into internal noc
 * requests which fits the width of the noc.
 * - Routers: their role is to bring internal requests from one point to
 * another. There should be one router at each node of the noc, so that requests
 * can go anywhere.
 * - Targets: a target is a master IO interface which corresponds to final
 * destinations for internal requests. Once the destination is reached by a
 * router, the request is sent to the target by sending the request to the
 * interface.
 */
class FlooNoc : public vp::Component
{
  public:
    FlooNoc(vp::ComponentConf &config);
    ~FlooNoc();

    void reset(bool active);

    // Return the memory-mapped entry corresponding to the specified mapping.
    // Can be used to get destination coordinates associated to an address
    // location.
    Entry *get_entry(uint64_t base, uint64_t size);

    // Internal router information is stored inside the requests.
    // These constants give the indices where the information is stored in the
    // requests data.
    static constexpr int REQ_SRC_NI =
        0; // Pointer to network interface where the request was received
    static constexpr int REQ_BURST = 1; // Burst received from network interface
    static constexpr int REQ_WIDE =
        2; // Indicates if a request is a wide request
           // or not. 1 for wide, 0 for narrow
    static constexpr int REQ_IS_ADDRESS =
        3; // Indicates if the request is a AR/AW request or not. 1 for address,
           // 0 for data
    static constexpr int REQ_DEST_ID =
        4; // Node ID of the destination target for flexible topologies
    static constexpr int REQ_INJECT_TIME =
        5; // Cycle timestamp when the packet was injected into the NoC
    static constexpr int REQ_SRC_ID =
        6; // Node ID of the network interface which injected the request.
           // Needed by collective fan-out, which is relative to both source
           // and destination (unicast routing only ever needs REQ_DEST_ID).
    static constexpr int REQ_COLL_OP =
        7; // [Collective Only] Collective operation code for this request, 
           // 0 (COLL_OP_UNICAST) for a normal, non-collective request.
    static constexpr int REQ_COLL_MASK =
        8; // [Collective Only] Mask for collective primitives.
    static constexpr int REQ_COLL_STATE =
        9; // [Collective Only] Stores the current collective state.
           // 0 is reserved as "freshly injected".
    static constexpr int REQ_PARENT =
        10; // [Collective Only] Pointer to the original request it forked from,
             // nullptr for the root request.
    static constexpr int REQ_PEND_KIDS =
        11; // [Collective Only] How many kids to be waited for response.
    static constexpr int REQ_NB_ARGS =
        12; // Number of request data required by this model

    // Collective operation codes for REQ_COLL_OP, aligned with FlooNoC
    // RTL's collect_reserved_op_e
    static constexpr int COLL_OP_UNICAST = 0;   // Not a collective (passthrough)
    static constexpr int COLL_OP_MULTICAST = 1; // Broadcast, no compute
    // @TODO Values 2-5 reserved for future structural ops (LsbAnd/SelectAW/CollectB/SeqAW)
    static constexpr int COLL_OP_FIRST_ARITH_OP =
        6; // First arithmetic (reduction) op code
    static constexpr int COLL_OP_REDADD_UINT16 = 6;
    static constexpr int COLL_OP_REDADD_INT16 = 7;
    static constexpr int COLL_OP_REDADD_FP16 = 8;
    static constexpr int COLL_OP_REDMAX_UINT16 = 9;
    static constexpr int COLL_OP_REDMAX_INT16 = 10;
    static constexpr int COLL_OP_REDMAX_FP16 = 11;

    // [Collective Only] REQ_COLL_MASK Format: we divide the mask into sub-masks,
    // one per axis (XY in 2D, XYZ in 3D, or leaf-index ranges in a tree).
    // We assume 16 bits are enough to index within an axis. 
    // Sub-masks are packed as bits [16*i, 16*i+16): 0-15 for axis 0, 16-31 for axis 1, etc.
    // The collective routing policy determines how these sub-masks are used.
    static constexpr int COLL_MASK_AXIS_BITS = 16;
    static constexpr uint64_t COLL_MASK_AXIS_MASK =
        (1ULL << COLL_MASK_AXIS_BITS) - 1;

    // Extract the sub-mask for routing axis `axis` (0-indexed) from a
    // packed REQ_COLL_MASK value.
    static inline uint16_t coll_mask_axis(uint64_t mask, int axis)
    {
        return (uint16_t)((mask >> (axis * COLL_MASK_AXIS_BITS)) &
                          COLL_MASK_AXIS_MASK);
    }

    // Pack a single axis sub-mask into its slice of a REQ_COLL_MASK value,
    // leaving all other axes unchanged.
    static inline uint64_t coll_mask_set_axis(uint64_t mask, int axis,
                                              uint16_t axis_mask)
    {
        uint64_t shift = axis * COLL_MASK_AXIS_BITS;
        return (mask & ~(COLL_MASK_AXIS_MASK << shift)) |
               ((uint64_t)axis_mask << shift);
    }

    // Width in bytes of the noc. This is used to split incoming bursts into
    // internal requests of this width so that the bandwidth corresponds to the
    // width.
    uint64_t wide_width;
    uint64_t narrow_width;

    // Properties for generic topology support
    int nb_nodes;
    int router_degrees;
    std::vector<std::vector<int>> links; // source node, dest node, latency

    // Topology-specific collective fan-out policy, nullptr when disabled 
    CollectiveRoutingPolicy *collective_policy = nullptr;

  private:
    void router_init_neighbours(Router *router, std::vector<Router *> &routers);

    // [Collective Only] fp16 format descriptor
    static constexpr flexfloat_desc_t fp16_desc = {5, 10};

    // [Collective Only] Merges a forked collective child's response data
    // into its parent's buffer according to the parent's REQ_COLL_OP..
    void process_collective_operations(vp::IoReq *parent, vp::IoReq *req);

    // This block trace
    vp::Trace trace;
    // Set of memory-mapped entries, with one for each target. They give
    // information about each target (base address, size, position)
    std::vector<Entry> entries;
    std::vector<std::string> itf_names;
    // SIze of the routers input queues. Pushing more requests than this size
    // will block the output queue of the sender.
    int router_input_queue_size;
    // Array of routers of the noc, sorted by position from first line to last
    // line
    std::vector<Router *> req_routers;
    std::vector<Router *> rsp_routers;
    std::vector<Router *> wide_routers;
    // Array of network interfaces of the noc, sorted by position from first
    // line to last line
    std::vector<NetworkInterface *> network_interfaces;
};
