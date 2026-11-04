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

#pragma once

#include <cstdint>
#include <vector>
#include "../idma.hpp"

/**
 * @brief The on-the-fly compute engine of the back-end (idma_otf_compute),
 * between the byte-lane buffer and the write side.
 *
 * It takes beat-aligned input beats from the buffer (at most one per cycle)
 * and gives output bytes to the write side. One operation at a time; a new
 * configuration is only taken once the engine is empty (the RTL backend
 * interlock).
 *
 * - Transpose (idma_otf_transpose, two tile banks): NE = width / E input
 *   beats fill a bank, which then drains one transposed beat per cycle
 *   through an output register, while the other bank fills. The tiles are
 *   walked column tile first, then row tile; the edge tiles of the tensor
 *   come with a byte strobe masking the rows and columns outside it.
 * - MX quant (idma_otf_mxquant): 32 FP32 or FP16 elements make a block,
 *   packed as one E8M0 scale byte and 32 E5M2 bytes into a 33 + width byte
 *   buffer the write side pops.
 * - MX dequant (idma_otf_mxdequant): 33-byte blocks expand, one per cycle,
 *   to 32 FP32 or FP16 elements in a 256-byte buffer; full beats go out.
 *
 * What the engine produces in a cycle is visible to the write side the next
 * one (the RTL output registers).
 */
class IdmaComputeEngine
{
public:
    /// @param width Data path width in bytes.
    IdmaComputeEngine(int width=8);

    /// Resize the engine (drops its state).
    void configure(int width);
    /// Drop all state.
    void reset();

    /// True when the engine holds nothing.
    bool idle() const;
    /// Operation currently configured.
    const IdmaComputeCfg &cfg() const { return this->current; }
    /// Take a new configuration (the engine must be idle).
    void start(const IdmaComputeCfg &cfg);

    /// Internal work of the cycle (bank drain, block expansion), after the
    /// write side popped. Returns true when work remains for later cycles.
    bool step(int64_t now);

    /// True when an input beat can be taken in cycle @p now.
    bool can_accept(int64_t now) const;
    /// Take an input beat of @p count bytes.
    void push(const uint8_t *beat, int count, int64_t now);

    /// True when @p count output bytes are visible in cycle @p now.
    bool can_pop(int count, int64_t now) const;
    /// Give @p count output bytes and, when @p strb is not NULL, their
    /// strobe. Returns true when some of them must not be written.
    bool pop(uint8_t *bus, uint8_t *strb, int count, int64_t now);

private:
    /// Byte FIFO whose last insertion is visible from the next cycle.
    struct Pack
    {
        std::vector<uint8_t> bytes;
        int last_size = 0;
        int64_t last_cycle = -1;

        int visible(int64_t now) const
        {
            return (int)this->bytes.size() - (this->last_cycle == now ? this->last_size : 0);
        }
        void insert(const uint8_t *data, int size, int64_t now)
        {
            this->bytes.insert(this->bytes.end(), data, data + size);
            this->last_size = size;
            this->last_cycle = now;
        }
    };

    bool is_transpose() const { return this->current.op == IDMA_COMPUTE_TRANSPOSE; }
    bool is_quant() const
    {
        return this->current.op == IDMA_COMPUTE_MXQUANT
            || this->current.op == IDMA_COMPUTE_MXQUANT_FP16;
    }
    bool is_dequant() const
    {
        return this->current.op == IDMA_COMPUTE_MXDEQUANT
            || this->current.op == IDMA_COMPUTE_MXDEQUANT_FP16;
    }

    /// Transpose: store one input row into the filling bank.
    void tp_push(const uint8_t *beat, int64_t now);
    /// Transpose: move the next row of the draining bank to the output register.
    bool tp_step(int64_t now);
    /// MX quant: add the elements of one input beat to the block.
    void mxq_push(const uint8_t *beat, int64_t now);
    /// MX dequant: expand one block when enough input is there.
    bool mxdq_step(int64_t now);

    int width;
    int log2_width;
    IdmaComputeCfg current;

    // Transpose
    struct Bank
    {
        std::vector<uint8_t> tile;
        bool full = false;
        /// First cycle the bank may be drained / filled again.
        int64_t drain_cycle = 0;
        int64_t fill_cycle = 0;
        /// Element-size mode, valid rows and columns of an edge tile (0: all).
        int mode = 0;
        int rows = 0;
        int cols = 0;
    };
    Bank banks[2];
    int wr_bank = 0;
    int wr_cnt = 0;
    int rd_bank = 0;
    int rd_cnt = 0;
    /// Row tile and column tile being filled.
    int rtw = 0;
    int ntw = 0;
    /// Output register.
    bool out_valid = false;
    int64_t out_cycle = 0;
    std::vector<uint8_t> out_data;
    std::vector<uint8_t> out_strb;
    bool out_masked = false;

    // MX quant: elements of the block being gathered (FP32 bits)
    uint32_t elems[32];
    int fill = 0;
    // MX dequant: input bytes not expanded yet, and whether a beat may come
    // in this cycle (decided on the state at the start of the cycle)
    std::vector<uint8_t> in_bytes;
    int64_t in_ready_cycle = -1;
    // MX quant and dequant output
    Pack pack;
};
