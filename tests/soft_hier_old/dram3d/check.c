// Copyright (C) 2026 ETH Zurich and University of Bologna
// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>

#define BASE UINT64_C(0x10000000000)
#define SPACE UINT64_C(0x1000000)
#define REMOTE 0x30000000u
#define SYNC 0x40000000u
#define ENCODE(f, rs2, rs1, rd) (((f) << 25) | ((rs2) << 20) | ((rs1) << 15) | ((rd) << 7) | 0x2b)

static void copy(uint64_t dst, uint64_t src, unsigned size, int broadcast)
{
    register uint32_t a0 __asm__("a0") = dst;
    register uint32_t a1 __asm__("a1") = dst >> 32;
    register uint32_t a2 __asm__("a2") = src;
    register uint32_t a3 __asm__("a3") = src >> 32;
    register uint32_t a4 __asm__("a4") = size;
    __asm__ volatile (".word %0" :: "i"(ENCODE(0, 13, 12, 0)), "r"(a2), "r"(a3) : "memory");
    __asm__ volatile (".word %0" :: "i"(ENCODE(1, 11, 10, 0)), "r"(a0), "r"(a1) : "memory");
    if (broadcast) {
        __asm__ volatile (".word %0" :: "i"(ENCODE(5, 0, 0, 0)) : "memory");
        __asm__ volatile (".word %1" : "=r"(a0) : "i"(ENCODE(3, 1, 14, 10)), "r"(a4) : "memory");
    } else {
        __asm__ volatile (".word %1" : "=r"(a0) : "i"(ENCODE(2, 0, 14, 10)), "r"(a4) : "memory");
    }
    __asm__ volatile ("1: .word %0\nbnez t0, 1b\nfence rw, rw"
        :: "i"(ENCODE(4, 2, 0, 5)) : "t0", "memory");
}

static void print(const char *s)
{
    while (*s) *(volatile unsigned *)0x90000010 = *s++;
}

static void check(unsigned address, unsigned count, unsigned seed)
{
    volatile uint8_t *data = (volatile uint8_t *)address;
    for (unsigned i = 0; i < count; ++i) {
        if (data[i] != (uint8_t)(i * 37 + seed)) {
            print("DRAM3D_ROUTE_FAIL\n");
            *(volatile unsigned *)0x90000000 = 1;
            while (1) {}
        }
    }
}

static void barrier(unsigned cid, unsigned phase)
{
    /* Above the model's initial 0x57575757 fill, and monotonic so a faster
       participant cannot make a slower one miss an earlier arrival. */
    phase += 0x80000000u;
    *(volatile unsigned *)(SYNC + cid * 192 + 64) = phase;
    for (unsigned i = 0; i < 4; ++i)
        while (*(volatile unsigned *)(SYNC + i * 192 + 64) < phase) {}
}

void main(void)
{
    unsigned cid = *(volatile unsigned *)0x20000000;
    for (unsigned other = 0; other < 4; ++other) {
        copy(0x2000, BASE + other * SPACE + 0x101, 1293, 0);
        check(0x2000, 1293, 19 + other);
        copy(0x2000, REMOTE + other * 0x10000 + 0x301, 259, 0);
        check(0x2000, 259, 41 + other);
    }
    /* One DMA burst crosses two channel apertures. */
    copy(0x2000, BASE + SPACE - 17, 66, 0);
    check(0x2000, 66, 91);

    for (unsigned i = 0; i < 259; ++i)
        *(volatile uint8_t *)(0x2000 + i) = i * 37 + cid + 101;
    copy(BASE + ((cid + 1) % 4) * SPACE + 0x4003, 0x2000, 259, 0);
    barrier(cid, 1);
    copy(0x2000, BASE + cid * SPACE + 0x4003, 259, 0);
    check(0x2000, 259, (cid + 3) % 4 + 101);

    barrier(cid, 2);
    if (cid == 0) {
        for (unsigned i = 0; i < 259; ++i)
            *(volatile uint8_t *)(0x2000 + i) = i * 37 + 123;
        copy(REMOTE + 0x5000, 0x2000, 259, 1);
    }
    barrier(cid, 3);
    check(0x5000, 259, 123);
    /* The same cluster endpoint still selects DRAM after a TCDM collective. */
    copy(0x2000, BASE + cid * SPACE + 0x101, 1293, 0);
    check(0x2000, 1293, 19 + cid);
    barrier(cid, 4);
    if (cid == 0) {
        print("DRAM3D_ROUTE_PASS\n");
        *(volatile unsigned *)0x90000000 = 0;
    }
    while (1) {}
}

__asm__(".section .text.start\n.global _start\n_start:\n"
        "li sp, 0x1001fff0\ncall main\n1: j 1b\n");
