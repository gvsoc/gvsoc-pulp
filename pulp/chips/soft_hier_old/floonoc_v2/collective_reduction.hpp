// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace softhier_collective {

inline float from_half(uint16_t h)
{
    unsigned exponent = (h >> 10) & 31, fraction = h & 1023;
    float value;
    if (exponent == 31) value = fraction ? NAN : INFINITY;
    else if (exponent == 0) value = std::ldexp(float(fraction), -24);
    else value = std::ldexp(float(1024 + fraction), int(exponent) - 25);
    return h & 0x8000 ? -value : value;
}

// IEEE binary16, round to nearest with ties to even (including subnormals).
inline uint16_t to_half(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    uint16_t sign = (bits >> 16) & 0x8000;
    unsigned exponent = (bits >> 23) & 255, fraction = bits & 0x7fffff;
    if (exponent == 255) return sign | 0x7c00 | (fraction ? 0x200 : 0);
    int e = int(exponent) - 127 + 15;
    if (e >= 31) return sign | 0x7c00;
    if (e < -10) return sign;
    unsigned shift = 13;
    if (e <= 0) { fraction |= 0x800000; shift += 1 - e; e = 0; }
    unsigned rounded = fraction >> shift;
    unsigned remainder = fraction & ((1u << shift) - 1);
    unsigned halfway = 1u << (shift - 1);
    rounded += remainder > halfway || (remainder == halfway && (rounded & 1));
    return sign | uint16_t((unsigned(e) << 10) + rounded);
}

inline void combine(uint8_t type, uint8_t *dst, const uint8_t *src, uint64_t size)
{
    for (uint64_t i = 0; i < size; i += 2)
    {
        uint16_t a, b, result;
        std::memcpy(&a, dst + i, 2);
        std::memcpy(&b, src + i, 2);
        switch (type)
        {
            case 2: case 3: result = uint16_t(unsigned(a) + unsigned(b)); break;
            case 4: result = to_half(from_half(a) + from_half(b)); break;
            case 5: result = a > b ? a : b; break;
            case 6: result = int16_t(a) > int16_t(b) ? a : b; break;
            case 7: result = to_half(std::fmax(from_half(a), from_half(b))); break;
            default: result = a; break;
        }
        std::memcpy(dst + i, &result, 2);
    }
}
}
