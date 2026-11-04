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

#include <algorithm>
#include <cstring>
#include "idma_compute.hpp"

// MX block geometry (idma_pkg): 32 elements, one scale byte + 32 E5M2 bytes
#define MX_BLOCK_ELEMS 32
#define MX_BLOCK_BYTES 33
// Dequant expansion buffer: two FP32 blocks
#define MX_DQ_BUF_SIZE 256
#define E5M2_BIAS 15



// The FP conversions of idma_float_pkg and of the MX units, bit for bit

// Block scale: the largest exponent of the block, Inf / NaN excluded, minus
// the FP32 and E5M2 biases, saturated to a signed byte
static int mx_block_scale(const uint32_t *elems)
{
    int max_exp = 0;
    for (int i = 0; i < MX_BLOCK_ELEMS; i++)
    {
        int exp = (elems[i] >> 23) & 0xff;
        if (exp != 0xff && exp > max_exp) max_exp = exp;
    }
    int scale = max_exp - 127 - E5M2_BIAS;
    return std::max(-128, std::min(127, scale));
}

// FP32 -> E5M2 under the block scale, round to nearest even, saturating
static uint8_t mx_quant_elem(uint32_t f, int scale)
{
    uint8_t sign = (f >> 31) << 7;
    int expf = (f >> 23) & 0xff;
    uint32_t manf = f & 0x7fffff;

    if (expf == 0 && manf == 0) return sign;
    if (expf == 0xff && manf != 0) return sign | (0x1f << 2) | 1;
    if (expf == 0xff) return sign | (0x1e << 2) | 3;

    int scaled = expf - 127 - scale;
    uint32_t full = (1 << 23) | manf;

    if (scaled > 15) return sign | (0x1e << 2) | 3;

    if (scaled >= -14)
    {
        int rounded = (full >> 21) & 0x7;
        bool guard = (full >> 20) & 1;
        bool sticky = (full & 0xfffff) != 0;
        if (guard && ((rounded & 1) || sticky)) rounded++;
        int carry = (rounded >> 3) & 1;
        int oexp = scaled + 15 + carry;
        int mmant = rounded & 3;
        if (oexp > 30)
        {
            oexp = 30;
            mmant = 3;
        }
        return sign | (oexp << 2) | mmant;
    }

    // Subnormal E5M2
    if (scaled < -17) return sign;
    int sh = 21 + (-14 - scaled);
    uint32_t kept = full >> sh;
    bool guard = (full >> (sh - 1)) & 1;
    bool sticky = (full & ((1u << (sh - 1)) - 1)) != 0;
    if (guard && ((kept & 1) || sticky)) kept++;
    if (kept == 0) return sign;
    if (kept < 4) return sign | kept;
    return sign | (1 << 2);
}

// E5M2 -> FP32 with the block scale applied
static uint32_t mx_dequant_elem(uint8_t b, int scale)
{
    uint32_t sign = (uint32_t)(b >> 7) << 31;
    int e5 = (b >> 2) & 0x1f;
    int m = b & 3;
    int base = e5 == 0 ? 111 + (m >> 1) : 112 + e5;
    int es = base + scale;
    uint32_t om = e5 == 0 ? (uint32_t)((m >> 1) & m & 1) << 22 : (uint32_t)m << 21;

    if (e5 == 0 && m == 0) return sign;
    if (e5 == 0x1f) return m == 0 ? sign | 0x7f800000 : 0x7fc00000;
    if (es <= 0) return sign;
    if (es >= 255) return sign | (0xfe << 23) | 0x7fffff;
    return sign | ((uint32_t)es << 23) | om;
}

// FP16 -> FP32, exact
static uint32_t fp16_to_fp32(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) << 31;
    int exp16 = (h >> 10) & 0x1f;
    uint32_t man16 = h & 0x3ff;

    if (exp16 == 0x1f) return man16 == 0 ? sign | 0x7f800000 : sign | 0x7f800000 | (1 << 22) | (man16 << 12);
    if (exp16 == 0 && man16 == 0) return sign;
    if (exp16 == 0)
    {
        int lz = 0;
        while (lz < 9 && !(man16 & (0x200 >> lz))) lz++;
        uint32_t exp32 = (127 - 15) - lz;
        uint32_t man_norm = (man16 << (lz + 1)) & 0x3ff;
        return sign | (exp32 << 23) | (man_norm << 13);
    }
    return sign | ((uint32_t)(exp16 + 127 - 15) << 23) | (man16 << 13);
}

// FP32 -> FP16, round to nearest even, overflow to Inf
static uint16_t fp32_to_fp16(uint32_t f)
{
    uint16_t sign = (f >> 31) << 15;
    int exp32 = (f >> 23) & 0xff;
    uint32_t man32 = f & 0x7fffff;

    if (exp32 == 0xff) return man32 != 0 ? sign | (0x1f << 10) | 0x200 : sign | (0x1f << 10);
    if (exp32 == 0) return sign;
    int unb = exp32 - 127;
    if (unb > 15) return sign | (0x1f << 10);
    if (unb >= -14)
    {
        uint32_t man16 = man32 >> 13;
        uint32_t rest = man32 & 0x1fff;
        uint32_t rounded = man16 + ((rest > 0x1000) || (rest == 0x1000 && (man16 & 1)));
        if (rounded & 0x400)
        {
            if (unb == 15) return sign | (0x1f << 10);
            return sign | ((unb + 15 + 1) << 10);
        }
        return sign | ((unb + 15) << 10) | rounded;
    }
    if (unb < -25) return sign;
    uint32_t full = (1 << 24) | (man32 << 1);
    int sh = -14 - unb;
    uint32_t man16 = (full >> (sh + 14)) & 0x3ff;
    bool guard = (full >> (sh + 13)) & 1;
    bool sticky = (full & ((1u << (sh + 13)) - 1)) != 0;
    uint32_t rounded = man16 + (guard && ((man16 & 1) || sticky));
    return sign | ((rounded & 0x400) ? (1 << 10) : rounded);
}



IdmaComputeEngine::IdmaComputeEngine(int width, bool full_duplex)
{
    this->configure(width, full_duplex);
}



void IdmaComputeEngine::configure(int width, bool full_duplex)
{
    this->width = width;
    this->full_duplex = full_duplex;
    this->log2_width = 0;
    while ((1 << this->log2_width) < width)
    {
        this->log2_width++;
    }
    for (Bank &bank: this->banks)
    {
        bank.tile.assign(width * width, 0);
    }
    this->out_data.assign(width, 0);
    this->out_strb.assign(width, 0);
    this->reset();
}



void IdmaComputeEngine::reset()
{
    this->current = IdmaComputeCfg();
    for (Bank &bank: this->banks)
    {
        bank.full = false;
        bank.drain_cycle = 0;
        bank.fill_cycle = 0;
    }
    this->wr_bank = this->wr_cnt = 0;
    this->rd_bank = this->rd_cnt = 0;
    this->rtw = this->ntw = 0;
    this->out_valid = false;
    this->fill = 0;
    this->in_bytes.clear();
    this->in_ready_cycle = -1;
    this->pack = Pack();
}



bool IdmaComputeEngine::idle() const
{
    return !this->banks[0].full && !this->banks[1].full && this->wr_cnt == 0 && !this->out_valid
        && this->fill == 0 && this->in_bytes.empty() && this->pack.bytes.empty();
}



void IdmaComputeEngine::start(const IdmaComputeCfg &cfg)
{
    this->reset();
    this->current = cfg;
}



bool IdmaComputeEngine::step(int64_t now)
{
    if (this->is_transpose())
    {
        return this->tp_step(now);
    }
    if (this->is_dequant())
    {
        return this->mxdq_step(now);
    }
    return false;
}



bool IdmaComputeEngine::can_accept(int64_t now) const
{
    if (this->is_transpose())
    {
        const Bank &bank = this->banks[this->wr_bank];
        return !bank.full && now >= bank.fill_cycle;
    }
    if (this->is_quant())
    {
        // Room for a block is only needed by the beat completing one
        int nelems = this->current.op == IDMA_COMPUTE_MXQUANT_FP16 ? this->width / 2
            : this->width / 4;
        bool completing = this->fill + nelems >= MX_BLOCK_ELEMS;
        return !completing || (int)this->pack.bytes.size() <= this->width;
    }
    if (this->is_dequant())
    {
        return this->in_ready_cycle == now;
    }
    return false;
}



void IdmaComputeEngine::push(const uint8_t *beat, int count, int64_t now)
{
    if (this->is_transpose())
    {
        this->tp_push(beat, now);
    }
    else if (this->is_quant())
    {
        this->mxq_push(beat, now);
    }
    else if (this->is_dequant())
    {
        this->in_bytes.insert(this->in_bytes.end(), beat, beat + count);
        this->in_ready_cycle = -1;
    }
}



bool IdmaComputeEngine::can_pop(int count, int64_t now) const
{
    if (this->is_transpose())
    {
        return this->out_valid && now >= this->out_cycle;
    }
    return this->pack.visible(now) >= count;
}



bool IdmaComputeEngine::pop(uint8_t *bus, uint8_t *strb, int count, int64_t now)
{
    if (this->is_transpose())
    {
        memcpy(bus, this->out_data.data(), count);
        if (strb != nullptr)
        {
            memcpy(strb, this->out_strb.data(), count);
        }
        this->out_valid = false;
        return this->out_masked;
    }

    memcpy(bus, this->pack.bytes.data(), count);
    this->pack.bytes.erase(this->pack.bytes.begin(), this->pack.bytes.begin() + count);
    return false;
}



// The filling bank takes one row per beat; the bank is full after NE rows,
// drainable from the next cycle, and records whether it is an edge tile of
// the tensor (tiles walked column tile first)
void IdmaComputeEngine::tp_push(const uint8_t *beat, int64_t now)
{
    Bank &bank = this->banks[this->wr_bank];
    int mode = std::min(this->current.mode, this->log2_width);
    int log2_ne = this->log2_width - mode;
    int ne = 1 << log2_ne;

    memcpy(bank.tile.data() + this->wr_cnt * this->width, beat, this->width);
    this->wr_cnt++;
    if (this->wr_cnt < ne)
    {
        return;
    }

    int y_tiles = (this->current.tensor_m + ne - 1) >> log2_ne;
    int n_tiles = (this->current.tensor_n + ne - 1) >> log2_ne;
    bool last_y = this->rtw == y_tiles - 1;
    bool last_n = this->ntw == n_tiles - 1;

    bank.mode = mode;
    bank.rows = last_y ? this->current.tensor_m & (ne - 1) : 0;
    bank.cols = last_n ? this->current.tensor_n & (ne - 1) : 0;
    bank.full = true;
    bank.drain_cycle = now + 1;

    if (last_y && last_n)
    {
        this->rtw = this->ntw = 0;
    }
    else if (last_y)
    {
        this->rtw = 0;
        this->ntw++;
    }
    else
    {
        this->rtw++;
    }

    this->wr_cnt = 0;
    if (this->full_duplex) this->wr_bank ^= 1;
}



// Output row k of a tile is its column k: byte p is byte (k * E + p % E) of
// row p / E. The strobe keeps the elements inside the tensor.
bool IdmaComputeEngine::tp_step(int64_t now)
{
    Bank &bank = this->banks[this->rd_bank];
    if (!bank.full)
    {
        return false;
    }
    if (this->out_valid || now < bank.drain_cycle)
    {
        return true;
    }

    int mode = bank.mode;
    int elem_mask = (1 << mode) - 1;
    int k = this->rd_cnt;
    bool col_valid = bank.cols == 0 || k < bank.cols;

    this->out_masked = false;
    for (int p = 0; p < this->width; p++)
    {
        int row = p >> mode;
        int col = (k << mode) | (p & elem_mask);
        this->out_data[p] = bank.tile[row * this->width + col];
        bool valid = col_valid && (bank.rows == 0 || row < bank.rows);
        this->out_strb[p] = valid;
        this->out_masked |= !valid;
    }
    this->out_valid = true;
    this->out_cycle = now + 1;

    this->rd_cnt++;
    if (this->rd_cnt == (this->width >> mode))
    {
        bank.full = false;
        bank.fill_cycle = now + 1;
        this->rd_cnt = 0;
        if (this->full_duplex) this->rd_bank ^= 1;
    }

    return true;
}



// Elements in, widened to FP32; a completed block is packed as its scale
// and its 32 E5M2 bytes, visible next cycle
void IdmaComputeEngine::mxq_push(const uint8_t *beat, int64_t now)
{
    bool fp16 = this->current.op == IDMA_COMPUTE_MXQUANT_FP16;
    int nelems = fp16 ? this->width / 2 : this->width / 4;

    for (int e = 0; e < nelems; e++)
    {
        uint32_t value;
        if (fp16)
        {
            value = fp16_to_fp32(beat[e * 2] | (beat[e * 2 + 1] << 8));
        }
        else
        {
            value = beat[e * 4] | (beat[e * 4 + 1] << 8) | (beat[e * 4 + 2] << 16)
                | ((uint32_t)beat[e * 4 + 3] << 24);
        }

        this->elems[this->fill++] = value;
        if (this->fill == MX_BLOCK_ELEMS)
        {
            uint8_t block[MX_BLOCK_BYTES];
            int scale = mx_block_scale(this->elems);
            block[0] = (uint8_t)scale;
            for (int i = 0; i < MX_BLOCK_ELEMS; i++)
            {
                block[i + 1] = mx_quant_elem(this->elems[i], scale);
            }
            this->pack.insert(block, MX_BLOCK_BYTES, now);
            this->fill = 0;
        }
    }
}



// One block expands per cycle when its 33 bytes are in and the buffer has
// room; whether a new input beat may come in is decided on the state at the
// start of the cycle
bool IdmaComputeEngine::mxdq_step(int64_t now)
{
    bool fp16 = this->current.op == IDMA_COMPUTE_MXDEQUANT_FP16;
    int total = this->in_bytes.size();
    bool can_insert = (int)this->pack.bytes.size() <= MX_DQ_BUF_SIZE / 2;

    if (total < MX_BLOCK_BYTES || (can_insert && total < 2 * MX_BLOCK_BYTES))
    {
        this->in_ready_cycle = now;
    }

    if (total < MX_BLOCK_BYTES || !can_insert)
    {
        return total >= MX_BLOCK_BYTES;
    }

    uint8_t out[MX_BLOCK_ELEMS * 4];
    int scale = (int8_t)this->in_bytes[0];
    for (int i = 0; i < MX_BLOCK_ELEMS; i++)
    {
        uint32_t value = mx_dequant_elem(this->in_bytes[i + 1], scale);
        if (fp16)
        {
            uint16_t half = fp32_to_fp16(value);
            out[i * 2] = half;
            out[i * 2 + 1] = half >> 8;
        }
        else
        {
            out[i * 4] = value;
            out[i * 4 + 1] = value >> 8;
            out[i * 4 + 2] = value >> 16;
            out[i * 4 + 3] = value >> 24;
        }
    }
    this->pack.insert(out, fp16 ? MX_BLOCK_ELEMS * 2 : MX_BLOCK_ELEMS * 4, now);
    this->in_bytes.erase(this->in_bytes.begin(), this->in_bytes.begin() + MX_BLOCK_BYTES);

    return true;
}
