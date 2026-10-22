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
 * Authors: Rafael Medina, ETH (medinar@iis.ee.ethz.ch)
 */

#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

// Isolates topology-specific communication collective (multicast/reduction)
// fan-out decisions from the generic, topology-agnostic Router FSM.
class CollectiveRoutingPolicy
{
  public:
    virtual ~CollectiveRoutingPolicy() {}

    // node_id: this router's own node id.
    // src_id:  REQ_SRC_ID of the request (root of the collective).
    // mask:    REQ_COLL_MASK of the request.
    // state:   REQ_COLL_STATE of the request. 0 is reserved for "just
    //          injected, not yet routed".
    //
    // A router calls analyze() whenever it dequeues a request with
    // REQ_COLL_OP != COLL_OP_UNICAST, and gets back the set of neighbors it
    // must forward/fork the request to. These are pairs of (neighbor_node_id, next_state).
    // and additionally sets `local_member` if this router's own attached NI is itself
    // part of the collective (i.e. should receive/eject a copy locally).
    virtual void analyze(int node_id, int src_id, uint64_t mask, int state,
                         std::vector<std::pair<int, int>> &fanout,
                         bool &local_member) const = 0;
};

// 2D mesh multicast/reduction fan-out, XY or YX dimension-order.
// Coordinates are supplied once at construction (one (x, y) pair per
// router node id, as parsed from the floogen graph)
class MeshXYCollectivePolicy : public CollectiveRoutingPolicy
{
  public:
    // x_first: true routes/fans X-then-Y (XY), false Y-then-X (YX).
    // node_coords: node_coords.at(node_id) == {x, y} for every router node
    // id taking part in collectives.
    MeshXYCollectivePolicy(
        bool x_first,
        const std::unordered_map<int, std::pair<int, int>> &node_coords);

    void analyze(int node_id, int src_id, uint64_t mask, int state,
                std::vector<std::pair<int, int>> &fanout,
                bool &local_member) const override;

  private:
    bool x_first;
    std::unordered_map<int, std::pair<int, int>> node_coords;
    // (x, y) -> node_id, built once at construction for neighbor lookups.
    std::unordered_map<int64_t, int> coord_to_id;

    static int64_t pack_coord(int x, int y)
    {
        return ((int64_t)x << 32) | (uint32_t)y;
    }

    // Looks up the node id of the neighbor at (x, y). Returns false if there 
    // is no router there.
    bool lookup_neighbor(int x, int y, int &neighbor_id) const;
};
