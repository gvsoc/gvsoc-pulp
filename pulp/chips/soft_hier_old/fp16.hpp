// Copyright (C) 2026 ETH Zurich and University of Bologna
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <cstring>

static inline float soft_hier_fp16_to_float(uint16_t h)
{
    unsigned e = (h >> 10) & 31, m = h & 1023;
    if (e == 0) return (h & 0x8000 ? -1.0f : 1.0f) * float(m) * 0x1p-24f;
    uint32_t u = (uint32_t(h & 0x8000) << 16) | ((e == 31 ? 255 : e + 112) << 23) | (m << 13);
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

static inline uint16_t soft_hier_float_to_fp16(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    uint32_t sign = (u >> 16) & 0x8000, mant = u & 0x7fffff;
    int exp = int((u >> 23) & 255) - 112;
    if (exp >= 31) return sign | (exp == 143 && mant ? 0x7e00 : 0x7c00);
    if (exp <= 0) {
        if (exp < -10) return sign;
        mant |= 0x800000;
        unsigned shift = 14 - exp;
        return sign | ((mant + ((1u << (shift - 1)) - 1) + ((mant >> shift) & 1)) >> shift);
    }
    // Round to nearest, ties to even, carrying into the exponent when necessary.
    mant += 0xfff + ((mant >> 13) & 1);
    return sign | ((uint32_t(exp) << 10) + (mant >> 13));
}
