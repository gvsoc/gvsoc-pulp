// Copyright (C) 2026 ETH Zurich and University of Bologna
// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include "regions.h"

static void print(const char *s)
{
    while (*s) *(volatile uint32_t *)0x90000010 = *s++;
}

static void fail(uint32_t address)
{
    print("PRELOAD_FAIL address=");
    *(volatile uint32_t *)0x90000014 = address;
    print("\n");
    *(volatile uint32_t *)0x90000000 = 1;
    while (1) {}
}

static void check(uint32_t address, unsigned length, unsigned seed, int zero)
{
    for (unsigned i = 0; i < length;)
    {
        if (((address + i) & 3) == 0 && length - i >= 4)
        {
            uint32_t expected = 0;
            for (unsigned j = 0; j < 4; ++j)
                expected |= (uint32_t)(uint8_t)(zero ? 0 : ((i + j) * 37 + seed)) << (j * 8);
            if (*(volatile uint32_t *)(address + i) != expected) fail(address + i);
            i += 4;
        }
        else
        {
            if (*(volatile uint8_t *)(address + i) != (uint8_t)(zero ? 0 : (i * 37 + seed)))
                fail(address + i);
            ++i;
        }
    }
}

void main(void)
{
    unsigned cluster = *(volatile uint8_t *)0x20000000;
    for (unsigned i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i)
        if (cluster == 0 || regions[i].address < 0xc0000000)
            check(regions[i].address, regions[i].length, regions[i].seed, regions[i].zero);
    if (CHECK_REMOTE) check(0x01008003, 259, 101 + cluster, 0);

    *(volatile uint32_t *)(0x41000000 + cluster * 192) = 0x12340000 + cluster;
    if (cluster == 0)
    {
        for (unsigned i = 0; i < 4; ++i)
            while (*(volatile uint32_t *)(0x41000000 + i * 192) != 0x12340000 + i) {}
        print("PRELOAD_PASS\n");
        *(volatile uint32_t *)0x90000000 = 0;
    }
    while (1) {}
}

__asm__(".section .text.start\n"
        ".global _start\n"
        "_start:\n"
        "li sp, 0x1101fff0\n"
        "call main\n"
        "1: j 1b\n");
