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

#pragma once

#include <cstdint>
#include <deque>

/**
 * @brief Registered FIFO with fifo_v3 timing.
 *
 * Models the common_cells fifo_v3 (FALL_THROUGH = 0) used by the iDMA for its
 * request, r_dp_req, w_dp_req and w_last queues:
 *
 * - an entry pushed in cycle N is visible at the output from cycle N + 1
 *   (or later when the pusher asks for an extra delay);
 * - a slot freed by a pop in cycle N can only be refilled from cycle N + 1
 *   (the full flag is registered), so a full FIFO refuses a push even when
 *   it is popped in the same cycle.
 *
 * All methods take the current cycle so the timing does not depend on the
 * order in which the blocks of one cycle are evaluated.
 */
template<typename T>
class IdmaTimedFifo
{
public:
    IdmaTimedFifo(int depth=1) : depth(depth) {}

    void set_depth(int depth) { this->depth = depth; }
    int get_depth() const { return this->depth; }

    void reset()
    {
        this->queue.clear();
        this->last_pop_cycle = -1;
        this->popped_in_cycle = 0;
    }

    /// True when a push is accepted in cycle @p now.
    bool can_push(int64_t now) const
    {
        int popped = this->last_pop_cycle == now ? this->popped_in_cycle : 0;
        return (int)this->queue.size() + popped < this->depth;
    }

    /// Push an entry in cycle @p now; it is visible from now + 1 + extra_delay.
    void push(const T &entry, int64_t now, int extra_delay=0)
    {
        this->queue.push_back({entry, now + 1 + extra_delay});
    }

    bool empty() const { return this->queue.empty(); }
    int size() const { return (int)this->queue.size(); }

    /// True when the head entry can be consumed in cycle @p now.
    bool head_visible(int64_t now) const
    {
        return !this->queue.empty() && this->queue.front().visible_at <= now;
    }

    /// Cycle at which the head entry becomes visible (only when not empty).
    int64_t head_visible_at() const { return this->queue.front().visible_at; }

    T &head() { return this->queue.front().entry; }
    const T &head() const { return this->queue.front().entry; }

    /// Entry at position @p index from the head (0 = head).
    T &at(int index) { return this->queue[index].entry; }

    void pop(int64_t now)
    {
        this->queue.pop_front();
        if (this->last_pop_cycle != now)
        {
            this->last_pop_cycle = now;
            this->popped_in_cycle = 0;
        }
        this->popped_in_cycle++;
    }

private:
    struct Entry
    {
        T entry;
        int64_t visible_at;
    };

    int depth;
    std::deque<Entry> queue;
    int64_t last_pop_cycle = -1;
    int popped_in_cycle = 0;
};
