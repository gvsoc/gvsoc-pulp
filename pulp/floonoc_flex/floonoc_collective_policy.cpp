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

#include "floonoc_collective_policy.hpp"
#include "floonoc_flex.hpp"

MeshXYCollectivePolicy::MeshXYCollectivePolicy(
    bool x_first,
    const std::unordered_map<int, std::pair<int, int>> &node_coords)
    : x_first(x_first), node_coords(node_coords)
{
    for (const auto &entry : this->node_coords)
    {
        int node_id = entry.first;
        int x = entry.second.first;
        int y = entry.second.second;
        this->coord_to_id[MeshXYCollectivePolicy::pack_coord(x, y)] = node_id;
    }
}

bool MeshXYCollectivePolicy::lookup_neighbor(int x, int y,
                                             int &neighbor_id) const
{
    auto it = this->coord_to_id.find(MeshXYCollectivePolicy::pack_coord(x, y));
    if (it == this->coord_to_id.end())
    {
        return false;
    }
    neighbor_id = it->second;
    return true;
}

void MeshXYCollectivePolicy::analyze(
    int node_id, int src_id, uint64_t mask, int state,
    std::vector<std::pair<int, int>> &fanout, bool &local_member) const
{
    fanout.clear();
    local_member = false;

    auto cur_it = this->node_coords.find(node_id);
    auto src_it = this->node_coords.find(src_id);
    if (cur_it == this->node_coords.end() || src_it == this->node_coords.end())
    {
        // Unknown node (e.g. src_id is not a router with known coords):
        // nothing this policy can do.
        return;
    }

    int cur_x = cur_it->second.first;
    int cur_y = cur_it->second.second;
    int src_x = src_it->second.first;
    int src_y = src_it->second.second;

    int mask_x = (int)FlooNoc::coll_mask_axis(mask, 0);
    int mask_y = (int)FlooNoc::coll_mask_axis(mask, 1);

    // The masked bits are don't-care around the source's own coordinate:
    // every member's coordinate lies within [*_min, *_max] on each axis.
    int x_max = src_x | mask_x;
    int x_min = src_x & ~mask_x;
    int y_max = src_y | mask_y;
    int y_min = src_y & ~mask_y;

    bool x_matched = (cur_x & ~mask_x) == (src_x & ~mask_x);
    bool y_matched = (cur_y & ~mask_y) == (src_y & ~mask_y);
    local_member = x_matched && y_matched;

    int neighbor_id;

    if (this->x_first)
    {
        // XY: spread along X first, while still on the source's row, then
        // along Y from any router whose column already matches.
        //
        // The cur_x >= src_x / cur_x <= src_x ensure a request is not forwarded 
        // back to the source.
        if (cur_y == src_y)
        {
            if (cur_x >= src_x && cur_x < x_max &&
                this->lookup_neighbor(cur_x + 1, cur_y, neighbor_id))
            {
                fanout.push_back({neighbor_id, 0});
            }
            if (cur_x <= src_x && cur_x > x_min &&
                this->lookup_neighbor(cur_x - 1, cur_y, neighbor_id))
            {
                fanout.push_back({neighbor_id, 0});
            }
        }
        if (x_matched)
        {
            if (cur_y >= src_y && cur_y < y_max &&
                this->lookup_neighbor(cur_x, cur_y + 1, neighbor_id))
            {
                fanout.push_back({neighbor_id, 0});
            }
            if (cur_y <= src_y && cur_y > y_min &&
                this->lookup_neighbor(cur_x, cur_y - 1, neighbor_id))
            {
                fanout.push_back({neighbor_id, 0});
            }
        }
    }
    else
    {
        // YX: mirror of the above with the X/Y roles swapped.
        if (cur_x == src_x)
        {
            if (cur_y >= src_y && cur_y < y_max &&
                this->lookup_neighbor(cur_x, cur_y + 1, neighbor_id))
            {
                fanout.push_back({neighbor_id, 0});
            }
            if (cur_y <= src_y && cur_y > y_min &&
                this->lookup_neighbor(cur_x, cur_y - 1, neighbor_id))
            {
                fanout.push_back({neighbor_id, 0});
            }
        }
        if (y_matched)
        {
            if (cur_x >= src_x && cur_x < x_max &&
                this->lookup_neighbor(cur_x + 1, cur_y, neighbor_id))
            {
                fanout.push_back({neighbor_id, 0});
            }
            if (cur_x <= src_x && cur_x > x_min &&
                this->lookup_neighbor(cur_x - 1, cur_y, neighbor_id))
            {
                fanout.push_back({neighbor_id, 0});
            }
        }
    }
}
