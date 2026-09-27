// Copyright 2026 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

// SoftHier data-path check. Every cluster fills a local buffer with a
// pattern of its own, then moves data to and from its ring neighbours
// through every path of the platform and checks it:
// - iDMA read from and write to a remote TCDM (wide NoC)
// - scalar remote stores and loads (narrow NoC)
// - vector loads / stores on the local TCDM (Spatz VLSU)
// - atomics on a remote TCDM (the global barrier, the error counter)
// Cluster 0 then times a few traffic patterns, prints the total number of
// errors and "SoftHier verify: OK" when there is none.

#include "softhier_runtime.h"
#include "softhier_printf.h"
#include "softhier_dma_pattern.h"

#define NB_WORDS     1024
#define NB_BYTES     (NB_WORDS * 4)
#define SRC_OFF      0x00000
#define DMA_RD_OFF   0x10000
#define DMA_WR_OFF   0x20000
#define NARROW_OFF   0x30000
#define VEC_OFF      0x40000
#define BW_SRC_OFF   0x50000
#define BW_DST_OFF   0x70000
#define BW_BYTES     0x10000
#define ERRORS_OFF   0x80000

static uint32_t pattern(uint32_t cid, uint32_t i)
{
    return (cid << 24) ^ (i * 2654435761u);
}

static uint32_t next_cluster(uint32_t cid) { return (cid + 1) % ARCH_NUM_CLUSTER; }
static uint32_t prev_cluster(uint32_t cid) { return (cid + ARCH_NUM_CLUSTER - 1) % ARCH_NUM_CLUSTER; }

static uint32_t check(uint32_t addr, uint32_t cid, uint32_t nb_words)
{
    volatile uint32_t *buffer = (volatile uint32_t *)addr;
    uint32_t errors = 0;
    for (uint32_t i = 0; i < nb_words; i++)
    {
        if (buffer[i] != pattern(cid, i)) errors++;
    }
    return errors;
}

static void vec_copy(uint32_t *dst, uint32_t *src, uint32_t n)
{
    while (n > 0)
    {
        uint32_t vl;
        asm volatile("vsetvli %0, %1, e32, m8, ta, ma" : "=r"(vl) : "r"(n));
        asm volatile("vle32.v v8, (%0)" :: "r"(src) : "memory");
        asm volatile("vse32.v v8, (%0)" :: "r"(dst) : "memory");
        src += vl;
        dst += vl;
        n -= vl;
    }
    asm volatile("fence" ::: "memory");
}

// The timed phases are measured by the core driving the iDMA of cluster 0,
// so that the timer starts when its transfers do.
static int is_timer_core(void)
{
    return flex_get_cluster_id() == 0 && flex_is_dm_core();
}

int main()
{
    uint32_t cid = flex_get_cluster_id();
    uint32_t core = flex_get_core_id();
    uint32_t errors = 0;

    flex_global_barrier_init();

    // Error counter, in the TCDM of cluster 0
    if (cid == 0 && flex_is_dm_core())
    {
        *(volatile uint32_t *)local(ERRORS_OFF) = 0;
    }

    // Local source buffers
    if (flex_is_first_core())
    {
        volatile uint32_t *src = (volatile uint32_t *)local(SRC_OFF);
        for (uint32_t i = 0; i < NB_WORDS; i++) src[i] = pattern(cid, i);
    }
    flex_global_barrier_polling();

    // iDMA read from the next cluster
    if (flex_is_dm_core())
    {
        flex_dma_async_1d(local(DMA_RD_OFF), remote_cid(next_cluster(cid), SRC_OFF), NB_BYTES);
        flex_dma_async_wait_all();
        errors += check(local(DMA_RD_OFF), next_cluster(cid), NB_WORDS);
    }
    flex_global_barrier_polling();

    // iDMA write to the next cluster, checked by the receiver
    if (flex_is_dm_core())
    {
        flex_dma_async_1d(remote_cid(next_cluster(cid), DMA_WR_OFF), local(SRC_OFF), NB_BYTES);
        flex_dma_async_wait_all();
    }
    flex_global_barrier_polling();
    if (flex_is_dm_core())
    {
        errors += check(local(DMA_WR_OFF), prev_cluster(cid), NB_WORDS);
    }

    // Scalar remote stores to the next cluster, checked by the receiver, and
    // scalar remote loads from the previous one
    if (flex_is_first_core())
    {
        volatile uint32_t *remote = (volatile uint32_t *)remote_cid(next_cluster(cid), NARROW_OFF);
        for (uint32_t i = 0; i < 64; i++) remote[i] = pattern(cid, i);
        errors += check(remote_cid(prev_cluster(cid), SRC_OFF), prev_cluster(cid), 64);
    }
    flex_global_barrier_polling();
    if (flex_is_first_core())
    {
        errors += check(local(NARROW_OFF), prev_cluster(cid), 64);
    }

    // Vector copy on the local TCDM, one slice per core
    {
        uint32_t slice = NB_WORDS / ARCH_NUM_CORE_PER_CLUSTER;
        uint32_t first = core * slice;
        vec_copy((uint32_t *)local(VEC_OFF) + first, (uint32_t *)local(SRC_OFF) + first, slice);
    }
    flex_intra_cluster_sync();
    if (flex_is_first_core())
    {
        errors += check(local(VEC_OFF), cid, NB_WORDS);
    }

    // Timing: global barrier
    if (is_timer_core()) printf("[verify] 10 global barriers\n");
    flex_global_barrier_polling();
    if (is_timer_core()) flex_timer_start();
    for (int i = 0; i < 10; i++) flex_global_barrier_polling();
    if (is_timer_core()) flex_timer_end();

    // Timing: every cluster reads 64 KB from the next one at the same time
    if (is_timer_core()) printf("[verify] all clusters read 64 KB from their neighbour\n");
    flex_global_barrier_polling();
    if (is_timer_core()) flex_timer_start();
    if (flex_is_dm_core())
    {
        flex_dma_async_1d(local(BW_DST_OFF), remote_cid(next_cluster(cid), BW_SRC_OFF), BW_BYTES);
        flex_dma_async_wait_all();
    }
    flex_global_barrier_polling();
    if (is_timer_core()) flex_timer_end();

    // Timing: cluster 0 alone writes 64 KB to the farthest cluster ID
    if (is_timer_core()) printf("[verify] cluster 0 writes 64 KB to cluster %d\n", ARCH_NUM_CLUSTER - 1);
    flex_global_barrier_polling();
    if (is_timer_core()) flex_timer_start();
    if (cid == 0 && flex_is_dm_core())
    {
        flex_dma_async_1d(remote_cid((ARCH_NUM_CLUSTER - 1), BW_DST_OFF), local(BW_SRC_OFF), BW_BYTES);
        flex_dma_async_wait_all();
    }
    if (is_timer_core()) flex_timer_end();

    // Error report
    flex_global_barrier_polling();
    if (flex_is_first_core() && errors)
    {
        __atomic_fetch_add((uint32_t *)remote_cid(0, ERRORS_OFF), errors, __ATOMIC_RELAXED);
    }
    flex_global_barrier_polling();
    if (is_timer_core())
    {
        uint32_t total = *(volatile uint32_t *)local(ERRORS_OFF);
        printf("[verify] errors: %d\n", total);
        if (total == 0) printf("SoftHier verify: OK\n");
        else printf("SoftHier verify: FAILED\n");
    }
    flex_global_barrier_polling();
    flex_eoc_all(0);
    return 0;
}
