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
#include <vector>

/**
 * @brief The byte-lane buffer between the read and the write manager
 * (idma_dataflow_element).
 *
 * @p width independent byte FIFOs of depth @p depth, each a registered
 * passthrough_stream_fifo:
 *
 * - a byte pushed in cycle N is visible to the write side from N + 1;
 * - a full lane accepts a push in the cycle it is popped (push-when-full),
 *   which is why the pops of a cycle must be applied before its pushes;
 * - at most one beat enters per cycle.
 *
 * Realignment is done by the barrel shifters around the buffer, which the
 * model folds into the lane indexing: on the read side lane i receives bus
 * byte (i + read_shift) % width, on the write side bus byte j takes lane
 * (j + write_shift) % width. With read_shift = src % width and write_shift =
 * -(dst % width) the same lane carries the same byte of the transfer on both
 * sides, whatever the alignments.
 *
 * Example, width 8, copying 6 bytes from source offset 3 to destination
 * offset 5: read_shift = 3, write_shift = -5 = 3 (mod 8). The read beat
 * carries the bytes in bus lanes 3..7 and 0 (of the next word); lane i of
 * the buffer takes bus byte (i + 3) % 8, so transfer bytes 0..5 land in lanes
 * 0..5. On the write side bus lane j takes buffer lane (j + 3) % 8: bus lanes
 * 5..7 take lanes 0..2 and bus lanes 0..2 of the next beat take lanes 3..5,
 * which is exactly where the bytes must go. The first write beat needs
 * bytes of two read beats, hence the extra cycle of misaligned copies.
 *
 * Storage is row-major (one row per depth slot). While every operation so
 * far moved all the lanes together (full masks, no shift), the lanes share
 * one head and count and a beat is a single row copy; the first partial or
 * shifted operation switches to per-lane bookkeeping until the buffer is
 * empty again.
 */
class IdmaByteLaneBuffer
{
public:
    /// @param width Lanes (the data path width in bytes).
    /// @param depth Entries per lane (the RTL BufferDepth).
    IdmaByteLaneBuffer(int width=8, int depth=3);

    /// Resize the buffer (drops its content).
    void configure(int width, int depth);
    /// Empty the buffer.
    void reset();

    /// Lanes (the data path width in bytes).
    int get_width() const { return this->width; }

    /// True when a beat whose lanes are @p mask_in (lane space) fits now.
    bool can_push(uint64_t mask_in, int64_t now) const;

    /// Push the lanes of @p mask_in from a full-width bus word; lane i takes
    /// bus[(i + shift) % width].
    void push(uint64_t mask_in, const uint8_t *bus, int shift, int64_t now);

    /// True when every bus lane of @p mask_out (bus space) has a visible byte
    /// in lane (j + shift) % width.
    bool can_pop(uint64_t mask_out, int shift, int64_t now) const;

    /// Pop the bytes of @p mask_out into a full-width bus word; bus[j] takes
    /// lane (j + shift) % width. Unmasked bus bytes are left untouched.
    void pop(uint64_t mask_out, int shift, uint8_t *bus, int64_t now);

    /// True when no lane holds a byte.
    bool empty() const { return this->bytes == 0; }

    /// Bytes held (visible or not).
    int fill() const { return this->bytes; }

private:
    /// Bookkeeping of one byte FIFO (a lane).
    struct Lane
    {
        /// Slot of the oldest byte and number of bytes held.
        int head = 0;
        int count = 0;
        /// Cycle of the last push: that byte is not visible yet in it.
        int64_t last_push_cycle = -1;
    };

    /// Bytes visible in lane @p lane in cycle @p now.
    int visible(int lane, int64_t now) const
    {
        const Lane &l = this->lanes[lane];
        return l.count - (l.last_push_cycle == now ? 1 : 0);
    }
    /// The width bytes of one depth slot (byte i of it belongs to lane i).
    uint8_t *row(int slot) { return this->rows.data() + slot * this->width; }
    const uint8_t *row(int slot) const { return this->rows.data() + slot * this->width; }
    /// Leave the uniform state: give every lane the shared head and count.
    void split_lanes();

    /// Lanes and entries per lane.
    int width;
    int depth;
    /// Mask with one bit per lane.
    uint64_t full_mask;
    /// Storage, depth rows of width bytes.
    std::vector<uint8_t> rows;
    /// Per-lane bookkeeping, valid while not uniform.
    std::vector<Lane> lanes;
    /// Shared bookkeeping while all lanes move together.
    bool uniform = true;
    Lane shared;
    /// Bytes held in the whole buffer.
    int bytes = 0;
    /// Cycle of the last accepted beat (one beat per cycle).
    int64_t last_beat_cycle = -1;
};
