/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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
 * Authors: Germain Haugou (germain.haugou@gmail.com)
 */

#include <cstring>
#include "idma_buffer.hpp"



IdmaByteLaneBuffer::IdmaByteLaneBuffer(int width, int depth)
{
    this->configure(width, depth);
}



void IdmaByteLaneBuffer::configure(int width, int depth)
{
    this->width = width;
    this->depth = depth;
    this->lane_mask = width - 1;
    this->rows.assign(width * depth, 0);
    this->lanes.assign(width, Lane());
    this->reset();
}



void IdmaByteLaneBuffer::reset()
{
    for (Lane &lane: this->lanes)
    {
        lane = Lane();
    }
    this->shared = Lane();
    this->uniform = true;
    this->bytes = 0;
    this->last_beat_cycle = -1;
}



void IdmaByteLaneBuffer::split_lanes()
{
    for (Lane &lane: this->lanes)
    {
        lane = this->shared;
    }
    this->uniform = false;
}



bool IdmaByteLaneBuffer::can_push(IdmaBeatBytes in, int shift, int64_t now) const
{
    if (this->last_beat_cycle == now)
    {
        return false;
    }

    if (this->uniform)
    {
        return this->shared.count < this->depth;
    }

    for (int j = in.first; j < in.first + in.count; j++)
    {
        if (this->lanes[(j - shift) & this->lane_mask].count >= this->depth)
        {
            return false;
        }
    }

    return true;
}



void IdmaByteLaneBuffer::push(IdmaBeatBytes in, const uint8_t *bus, int shift, int64_t now)
{
    this->last_beat_cycle = now;

    if (this->uniform)
    {
        if (this->is_full(in, shift))
        {
            // All lanes together: one row copy
            int slot = this->shared.head + this->shared.count;
            if (slot >= this->depth)
            {
                slot -= this->depth;
            }
            std::memcpy(this->row(slot), bus, this->width);
            this->shared.count++;
            this->shared.last_push_cycle = now;
            this->bytes += this->width;
            return;
        }
        this->split_lanes();
    }

    for (int j = in.first; j < in.first + in.count; j++)
    {
        int i = (j - shift) & this->lane_mask;
        Lane &lane = this->lanes[i];
        int slot = lane.head + lane.count;
        if (slot >= this->depth)
        {
            slot -= this->depth;
        }
        this->row(slot)[i] = bus[j];
        lane.count++;
        lane.last_push_cycle = now;
        this->bytes++;
    }
}



bool IdmaByteLaneBuffer::can_pop(IdmaBeatBytes out, int shift, int64_t now) const
{
    if (this->uniform)
    {
        int visible = this->shared.count - (this->shared.last_push_cycle == now ? 1 : 0);
        return visible > 0;
    }

    for (int j = out.first; j < out.first + out.count; j++)
    {
        if (this->visible((j + shift) & this->lane_mask, now) <= 0)
        {
            return false;
        }
    }

    return true;
}



void IdmaByteLaneBuffer::pop(IdmaBeatBytes out, int shift, uint8_t *bus, int64_t now)
{
    if (this->uniform)
    {
        if (this->is_full(out, shift))
        {
            std::memcpy(bus, this->row(this->shared.head), this->width);
            this->shared.head++;
            if (this->shared.head == this->depth)
            {
                this->shared.head = 0;
            }
            this->shared.count--;
            this->bytes -= this->width;
            return;
        }
        this->split_lanes();
    }

    for (int j = out.first; j < out.first + out.count; j++)
    {
        int i = (j + shift) & this->lane_mask;
        Lane &lane = this->lanes[i];
        bus[j] = this->row(lane.head)[i];
        lane.head++;
        if (lane.head == this->depth)
        {
            lane.head = 0;
        }
        lane.count--;
        this->bytes--;
    }

    // Back to the shared bookkeeping once drained
    if (this->bytes == 0)
    {
        this->shared = Lane();
        this->uniform = true;
    }
}
